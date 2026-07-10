#include "ImageLoader.h"
#include "utils/ImageUtils.h"

#include <QtConcurrent>
#include <QMutexLocker>
#include <QFutureWatcher>
#include <QThread>
#include <QMetaObject>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QDeadlineTimer>
#include <QFileInfoList>

#include <tuple>
#include <utility>

namespace
{
constexpr int kPriorityBackground = 0;
constexpr int kPriorityPrefetch = 1;
constexpr int kPriorityVisible = 2;
constexpr int kMaxPendingDiskWrites = 16;
constexpr qint64 kMaxPendingDiskWriteBytes = 32LL * 1024LL * 1024LL;
constexpr qint64 kDiskCacheBudgetBytes = 512LL * 1024LL * 1024LL;
}

ImageLoader::ImageLoader(QObject *parent)
    : QObject(parent),
      m_thumbnailCancel(std::make_shared<CancellationState>()),
      m_imageCancel(std::make_shared<CancellationState>()),
      m_decodePool(std::make_unique<QThreadPool>()),
      m_imageDecodePool(std::make_unique<QThreadPool>()),
      m_diskCachePool(std::make_unique<QThreadPool>())
{
    // More in-flight loads than CPU cores so network/slow-disk reads overlap
    // their I/O latency. Bounded so we don't flood a share with connections.
    m_decodePool->setMaxThreadCount(qMax(m_maxConcurrentLoads,
                                         qBound(4, QThread::idealThreadCount() * 2, 12)));
    m_decodePool->setExpiryTimeout(30000);
    // Keep full-resolution current-image loads independent from a saturated
    // thumbnail queue. One prefetch slot plus two visible slots gives rapid
    // navigation a guaranteed path to the decoder.
    m_imageDecodePool->setMaxThreadCount(m_maxConcurrentImageLoads);
    m_imageDecodePool->setExpiryTimeout(30000);
    // PNG compression/persistence must not extend time-to-first-paint or occupy
    // decode slots. A single bounded writer preserves ordering and avoids an I/O
    // burst while the decode pool is filling the viewport.
    m_diskCachePool->setMaxThreadCount(1);
    m_diskCachePool->setExpiryTimeout(30000);

    static std::atomic_flag maintenanceScheduled = ATOMIC_FLAG_INIT;
    if (!maintenanceScheduled.test_and_set(std::memory_order_acq_rel)) {
        const QFuture<void> future = QtConcurrent::run(m_diskCachePool.get(), []() {
            pruneDiskCache();
        });
        Q_UNUSED(future);
    }
}

ImageLoader::~ImageLoader()
{
    // Shutdown is an unconditional cancellation boundary: the normal keep-set
    // semantics must not preserve visible/selected paths while the destructor is
    // waiting for the pools to drain.
    {
        QMutexLocker locker(&m_thumbnailCancel->keepMutex);
        m_thumbnailCancel->keepPaths.clear();
    }
    {
        QMutexLocker locker(&m_imageCancel->keepMutex);
        m_imageCancel->keepPaths.clear();
    }
    m_thumbnailCancel->generation.fetch_add(1, std::memory_order_release);
    m_imageCancel->generation.fetch_add(1, std::memory_order_release);

    // Drop every queued runnable before waiting so the three pools share one
    // shutdown deadline rather than each adding its own timeout. Workers capture
    // only value data/shared state (never `this`), so a pool that is still stuck
    // in OS I/O can safely be detached after the overall deadline expires.
    if (m_decodePool) m_decodePool->clear();
    if (m_imageDecodePool) m_imageDecodePool->clear();
    if (m_diskCachePool) m_diskCachePool->clear();
    QDeadlineTimer shutdownDeadline(2000);
    auto drainWorkerPool = [&shutdownDeadline](std::unique_ptr<QThreadPool> &pool) {
        if (!pool) {
            return;
        }
        const qint64 remaining = qMax<qint64>(0, shutdownDeadline.remainingTime());
        if (!pool->waitForDone(static_cast<int>(remaining))) {
            // QImageReader/file I/O cannot be interrupted once inside the OS.
            // The runnable captures only value data/shared cancellation state;
            // detach the self-contained pool so teardown remains bounded.
            pool.release();
        }
    };
    drainWorkerPool(m_decodePool);
    drainWorkerPool(m_imageDecodePool);
    // Persistence is best-effort too: an active atomic write gets the same
    // bounded opportunity to finish, then its self-contained pool is detached.
    drainWorkerPool(m_diskCachePool);

    // The QFutureWatcher children's finished-slots capture `this`. Normally their
    // futures are finished; a pool detached after the timeout may still be inside
    // OS I/O. Delete every watcher now so neither case can later invoke a slot on
    // a half-destroyed object.
    const auto watchers = findChildren<QFutureWatcherBase *>();
    for (QFutureWatcherBase *watcher : watchers) {
        delete watcher;
    }
}

void ImageLoader::requestThumbnail(const QString &imagePath, const QSize &thumbnailSize)
{
    enqueueThumbnailRequest(imagePath, thumbnailSize, kPriorityVisible, false);
}

void ImageLoader::requestThumbnailBatch(const QStringList &imagePaths, const QSize &thumbnailSize,
                                        const QHash<QString, QDateTime> &sourceModifiedUtc)
{
    for (const QString &path : imagePaths) {
        enqueueThumbnailRequest(path, thumbnailSize, kPriorityBackground, true,
                                sourceModifiedUtc.value(path));
    }
}

void ImageLoader::requestThumbnailBatchVisibleFirst(const QStringList &imagePaths, const QSize &thumbnailSize,
                                                    const QHash<QString, QDateTime> &sourceModifiedUtc)
{
    for (const QString &path : imagePaths) {
        enqueueThumbnailRequest(path, thumbnailSize, kPriorityVisible, false,
                                sourceModifiedUtc.value(path));
    }
}

void ImageLoader::requestThumbnailBatchPrefetch(const QStringList &imagePaths, const QSize &thumbnailSize,
                                                const QHash<QString, QDateTime> &sourceModifiedUtc)
{
    for (const QString &path : imagePaths) {
        enqueueThumbnailRequest(path, thumbnailSize, kPriorityPrefetch, false,
                                sourceModifiedUtc.value(path));
    }
}

void ImageLoader::enqueueThumbnailRequest(const QString &imagePath,
                                          const QSize &thumbnailSize,
                                          int priority,
                                          bool highQuality,
                                          const QDateTime &sourceModifiedUtc)
{
    if (imagePath.isEmpty() || !thumbnailSize.isValid()) {
        return;
    }

    QImage cachedThumbnail;
    QSize cachedRequestedSize;
    bool cachedHighQuality = false;
    bool hasCached = false;
    bool shouldProcess = false;

    // Snapshot the strip-profile flag now so a runtime toggle can't make the
    // decode and the cache key disagree about whether the result is stripped.
    const bool ignoreColorProfile = m_ignoreColorProfile.load(std::memory_order_acquire);

    const QString key = memoryCacheKey(imagePath, thumbnailSize, highQuality, ignoreColorProfile);
    const QString highQualityKey = memoryCacheKey(imagePath, thumbnailSize, true, ignoreColorProfile);
    const QString fastKey = memoryCacheKey(imagePath, thumbnailSize, false, ignoreColorProfile);

    {
        QMutexLocker locker(&m_cacheMutex);
        ++m_metricRequests;

        auto cacheIt = m_thumbnailIndex.find(highQualityKey);
        if (cacheIt == m_thumbnailIndex.end() && !highQuality) {
            cacheIt = m_thumbnailIndex.find(fastKey);
        }
        if (cacheIt != m_thumbnailIndex.end()) {
            ThumbnailIter listIt = cacheIt.value();
            cachedThumbnail = listIt->thumbnail;
            hasCached = !cachedThumbnail.isNull();
            if (hasCached) {
                cachedRequestedSize = listIt->requestedSize;
                cachedHighQuality = listIt->highQuality;
                touchThumbnailEntryUnlocked(listIt);
                ++m_metricMemoryHits;
            }
        }

        if (!hasCached) {
            ThumbnailRequest request{key, imagePath, thumbnailSize, priority, highQuality,
                                     ignoreColorProfile, sourceModifiedUtc, 0};
            auto pendingIt = m_pendingRequests.find(key);
            if (pendingIt != m_pendingRequests.end()) {
                if (priority > pendingIt->priority) {
                    pendingIt->priority = priority;
                    // An in-flight request needs no second worker: the result is
                    // identical and will satisfy the newly-visible consumer.
                    if (!m_activeThumbnailRequests.contains(key)) {
                        enqueueRequestUnlocked(*pendingIt);
                        shouldProcess = true;
                    }
                }
            } else {
                request.token = m_nextRequestToken++;
                m_pendingRequests.insert(key, request);
                // A cancelled request for the same key can still be draining.
                // Its token cannot satisfy this new state; queue the replacement
                // as soon as the stale worker releases the key.
                if (!m_activeThumbnailRequests.contains(key)) {
                    enqueueRequestUnlocked(request);
                    shouldProcess = true;
                }
            }
        }
    }

    if (hasCached) {
        QMetaObject::invokeMethod(this, [this, imagePath, cachedThumbnail,
                                         cachedRequestedSize, cachedHighQuality,
                                         ignoreColorProfile]() {
            if (ignoreColorProfile !=
                m_ignoreColorProfile.load(std::memory_order_acquire)) {
                return;
            }
            emit thumbnailReady(imagePath, cachedThumbnail);
            emit thumbnailReadyDetailed(imagePath, cachedThumbnail,
                                        cachedRequestedSize, cachedHighQuality);
        }, Qt::QueuedConnection);
        return;
    }

    if (shouldProcess) {
        processQueue();
    }
}

void ImageLoader::enqueueRequestUnlocked(const ThumbnailRequest &request)
{
    if (request.priority >= kPriorityVisible) {
        m_highPriorityQueue.enqueue(request);
    } else if (request.priority == kPriorityPrefetch) {
        m_normalPriorityQueue.enqueue(request);
    } else {
        m_backgroundPriorityQueue.enqueue(request);
    }
}

void ImageLoader::processQueue()
{
    while (true) {
        ThumbnailRequest request;
        bool hasRequest = false;
        int capturedGeneration = 0;

        {
            QMutexLocker locker(&m_cacheMutex);
            if (m_activeLoads >= m_maxConcurrentLoads) {
                return;
            }

            auto tryDequeue = [this, &request, &hasRequest](QQueue<ThumbnailRequest> &queue) {
                while (!queue.isEmpty()) {
                    ThumbnailRequest candidate = queue.dequeue();
                    auto pendingIt = m_pendingRequests.constFind(candidate.key);
                    if (pendingIt == m_pendingRequests.constEnd()) {
                        continue;
                    }
                    if (pendingIt->priority != candidate.priority ||
                        pendingIt->token != candidate.token) {
                        continue;
                    }
                    request = candidate;
                    hasRequest = true;
                    return;
                }
            };

            tryDequeue(m_highPriorityQueue);
            if (!hasRequest) {
                tryDequeue(m_normalPriorityQueue);
            }
            if (!hasRequest) {
                tryDequeue(m_backgroundPriorityQueue);
            }
            if (!hasRequest) {
                return;
            }

            ++m_activeLoads;
            m_activeThumbnailRequests.insert(request.key, request.token);
            capturedGeneration = m_thumbnailCancel->generation.load(std::memory_order_acquire);
        }

        // The worker captures a shared_ptr to the cancellation state so it
        // stays valid even if ImageLoader is destroyed mid-decode. Before
        // the expensive decode it consults the live generation / keep-set
        // and self-aborts so fast folder switches don't burn CPU.
        auto cancelState = m_thumbnailCancel;

        auto future = QtConcurrent::run(m_decodePool.get(), [request, cancelState, capturedGeneration]() {
            const auto makeResult = [&request](const QImage &img,
                                               const QDateTime &mod,
                                               bool fromDisk,
                                               bool cancelled) {
                return std::make_tuple(request.key,
                                       request.imagePath,
                                       request.thumbnailSize,
                                       img,
                                       mod,
                                       request.highQuality,
                                       request.ignoreColorProfile,
                                       fromDisk,
                                       cancelled,
                                       request.token);
            };

            const auto isCancelled = [&]() {
                if (capturedGeneration ==
                    cancelState->generation.load(std::memory_order_acquire)) {
                    return false;
                }
                QMutexLocker locker(&cancelState->keepMutex);
                return !cancelState->keepPaths.contains(request.imagePath);
            };

            // Cheap pre-decode cancellation check. Skipping the disk
            // lookup on cancellation avoids even a filesystem stat for
            // queued-but-stale work.
            if (isCancelled()) {
                return makeResult(QImage(), QDateTime(), false, true);
            }

            // Prefer the mtime captured during the folder scan; only stat the
            // file ourselves when it wasn't provided (single requestThumbnail,
            // full-image path, or a filesystem that couldn't report it). This
            // removes one stat (a network round-trip) per thumbnail.
            const QDateTime modifiedUtc = request.sourceLastModifiedUtc.isValid()
                ? request.sourceLastModifiedUtc
                : sourceLastModifiedUtc(request.imagePath);

            if (QImage disk = tryLoadDiskCachedThumbnail(
                    request.imagePath,
                    request.thumbnailSize,
                    modifiedUtc,
                    request.highQuality,
                    request.ignoreColorProfile);
                !disk.isNull()) {
                return makeResult(disk, modifiedUtc, true, false);
            }

            // Re-check before the expensive decode in case the generation
            // moved while we were doing the disk lookup.
            if (isCancelled()) {
                return makeResult(QImage(), modifiedUtc, false, true);
            }

            const Qt::TransformationMode mode = request.highQuality
                ? Qt::SmoothTransformation
                : Qt::FastTransformation;
            QImage generated = ImageUtils::generateThumbnail(
                request.imagePath, request.thumbnailSize, mode,
                request.ignoreColorProfile);

            return makeResult(generated, modifiedUtc, false, false);
        });

        using ResultTuple = std::tuple<QString, QString, QSize, QImage, QDateTime,
                                       bool, bool, bool, bool, quint64>;
        auto *watcher = new QFutureWatcher<ResultTuple>(this);
        connect(watcher, &QFutureWatcher<ResultTuple>::finished, this,
                [this, watcher]() {
                    auto [key, imagePath, thumbnailSize, thumbnail, modifiedUtc,
                          highQuality, ignoreColorProfile, fromDisk, workerCancelled,
                          completedToken] = watcher->result();
                    watcher->deleteLater();

                    bool cancelledBeforeEmit = false;
                    bool shouldPersist = false;
                    {
                        QMutexLocker locker(&m_cacheMutex);
                        const auto activeIt = m_activeThumbnailRequests.constFind(key);
                        const bool ownsActive = activeIt != m_activeThumbnailRequests.constEnd()
                            && activeIt.value() == completedToken;
                        const auto pendingIt = m_pendingRequests.find(key);
                        const bool tokenMatches = ownsActive &&
                            pendingIt != m_pendingRequests.end()
                            && pendingIt->token == completedToken;

                        if (workerCancelled || !tokenMatches) {
                            // Worker self-aborted; nothing to cache and
                            // nothing to emit. Don't disturb m_pendingRequests:
                            // if the user re-issued the request after the
                            // cancel, the entry there belongs to that newer
                            // request and a fresh worker will service it.
                            cancelledBeforeEmit = true;
                            ++m_metricCancelled;
                        } else {
                            if (fromDisk && !thumbnail.isNull()) {
                                ++m_metricDiskHits;
                            } else {
                                ++m_metricDecodes;
                            }

                            m_pendingRequests.erase(pendingIt);
                            shouldPersist = !fromDisk && !thumbnail.isNull();
                        }

                        if (ownsActive) {
                            m_activeThumbnailRequests.remove(key);
                        }
                        m_activeLoads = qMax(0, m_activeLoads - 1);
                        auto currentIt = m_pendingRequests.constFind(key);
                        if (ownsActive && currentIt != m_pendingRequests.constEnd()) {
                            enqueueRequestUnlocked(*currentIt);
                        }
                    }

                    finishRequest(imagePath,
                                  thumbnailSize,
                                  workerCancelled ? QImage() : thumbnail,
                                  modifiedUtc,
                                  highQuality,
                                  ignoreColorProfile,
                                  cancelledBeforeEmit);
                    if (shouldPersist) {
                        scheduleDiskThumbnailWrite(imagePath, thumbnailSize,
                                                   modifiedUtc, thumbnail,
                                                   highQuality, ignoreColorProfile);
                    }
                    processQueue();
                });

        watcher->setFuture(future);
    }
}

void ImageLoader::finishRequest(const QString &imagePath,
                                const QSize &thumbnailSize,
                                const QImage &thumbnail,
                                const QDateTime &lastModifiedUtc,
                                bool highQuality,
                                bool ignoreColorProfile,
                                bool cancelledBeforeEmit)
{
    if (!cancelledBeforeEmit && !thumbnail.isNull()) {
        QMutexLocker locker(&m_cacheMutex);
        const QString key = memoryCacheKey(imagePath, thumbnailSize, highQuality, ignoreColorProfile);
        const qint64 bytes = imageByteSize(thumbnail);
        CacheEntry entry{key,
                         imagePath,
                         thumbnail,
                         thumbnailSize,
                         lastModifiedUtc,
                         highQuality,
                         ignoreColorProfile,
                         bytes};
        insertThumbnailEntryUnlocked(std::move(entry));
        trimCache();
    }

    if (!cancelledBeforeEmit) {
        emit thumbnailReady(imagePath, thumbnail);
        emit thumbnailReadyDetailed(imagePath, thumbnail, thumbnailSize, highQuality);
    }
}

void ImageLoader::requestImage(const QString &imagePath)
{
    enqueueImageRequest(imagePath, kPriorityVisible, true);
}

void ImageLoader::requestImageBatch(const QStringList &imagePaths)
{
    for (const QString &path : imagePaths) {
        requestImage(path);
    }
}

void ImageLoader::prefetchImages(const QStringList &imagePaths)
{
    for (const QString &path : imagePaths) {
        enqueueImageRequest(path, kPriorityPrefetch, false);
    }
}

void ImageLoader::enqueueImageRequest(const QString &imagePath, int priority, bool notify)
{
    if (imagePath.isEmpty()) {
        return;
    }

    QImage cachedImage;
    bool hasCached = false;
    bool shouldProcess = false;
    const bool ignoreColorProfile = m_ignoreColorProfile.load(std::memory_order_acquire);
    {
        QMutexLocker locker(&m_cacheMutex);
        auto cacheIt = m_imageIndex.find(imagePath);
        if (cacheIt != m_imageIndex.end()) {
            ImageIter listIt = cacheIt.value();
            cachedImage = listIt->image;
            hasCached = !cachedImage.isNull();
            if (hasCached) {
                touchImageEntryUnlocked(listIt);
            }
        }

        if (!hasCached) {
            auto pendingIt = m_pendingImageRequests.find(imagePath);
            if (pendingIt != m_pendingImageRequests.end()) {
                const bool priorityRaised = priority > pendingIt->priority;
                pendingIt->priority = qMax(pendingIt->priority, priority);
                pendingIt->notify = pendingIt->notify || notify;
                if (priorityRaised && !m_activeImageRequests.contains(imagePath)) {
                    enqueueImageRequestUnlocked(*pendingIt);
                    shouldProcess = true;
                }
            } else {
                ImageRequest request{imagePath, priority, notify, m_nextRequestToken++};
                m_pendingImageRequests.insert(imagePath, request);
                if (!m_activeImageRequests.contains(imagePath)) {
                    enqueueImageRequestUnlocked(request);
                    shouldProcess = true;
                }
            }
        }
    }

    if (hasCached) {
        if (notify) {
            QMetaObject::invokeMethod(this, [this, imagePath, cachedImage,
                                             ignoreColorProfile]() {
                if (ignoreColorProfile !=
                    m_ignoreColorProfile.load(std::memory_order_acquire)) {
                    return;
                }
                emit imageReady(imagePath, cachedImage);
            }, Qt::QueuedConnection);
        }
        return;
    }

    if (shouldProcess) {
        processImageQueue();
    }
}

void ImageLoader::enqueueImageRequestUnlocked(const ImageRequest &request)
{
    if (request.priority >= kPriorityVisible) {
        m_visibleImageQueue.enqueue(request);
    } else {
        m_prefetchImageQueue.enqueue(request);
    }
}

void ImageLoader::processImageQueue()
{
    while (true) {
        ImageRequest request;
        bool hasRequest = false;
        int capturedGeneration = 0;

        {
            QMutexLocker locker(&m_cacheMutex);
            if (m_activeImageLoads >= m_maxConcurrentImageLoads) {
                return;
            }

            auto takeCurrent = [this, &request, &hasRequest](QQueue<ImageRequest> &queue) {
                while (!queue.isEmpty()) {
                    const ImageRequest candidate = queue.dequeue();
                    const auto pendingIt = m_pendingImageRequests.constFind(candidate.imagePath);
                    if (pendingIt == m_pendingImageRequests.constEnd() ||
                        pendingIt->token != candidate.token ||
                        pendingIt->priority != candidate.priority) {
                        continue;
                    }
                    request = candidate;
                    hasRequest = true;
                    return;
                }
            };

            takeCurrent(m_visibleImageQueue);
            if (!hasRequest &&
                m_activeImagePrefetchLoads < m_maxConcurrentImagePrefetchLoads) {
                takeCurrent(m_prefetchImageQueue);
            }
            if (!hasRequest) {
                return;
            }

            ++m_activeImageLoads;
            if (request.priority < kPriorityVisible) {
                ++m_activeImagePrefetchLoads;
            }
            m_activeImageRequests.insert(request.imagePath, request.token);
            capturedGeneration = m_imageCancel->generation.load(std::memory_order_acquire);
        }

        auto cancelState = m_imageCancel;
        // Snapshot the strip-profile flag for this load so a runtime toggle
        // can't make the in-flight decode disagree with the user's intent.
        const bool ignoreColorProfile = m_ignoreColorProfile.load(std::memory_order_acquire);
        auto future = QtConcurrent::run(m_imageDecodePool.get(),
                                        [request, cancelState, capturedGeneration,
                                         ignoreColorProfile]() {
            const auto isCancelled = [&]() {
                if (capturedGeneration ==
                    cancelState->generation.load(std::memory_order_acquire)) {
                    return false;
                }
                QMutexLocker locker(&cancelState->keepMutex);
                return !cancelState->keepPaths.contains(request.imagePath);
            };
            if (isCancelled()) {
                return std::make_tuple(QImage(), true, ignoreColorProfile,
                                       request.token);
            }
            QImage image(request.imagePath);
            if (ignoreColorProfile) {
                ImageUtils::stripColorProfile(image);
            }
            if (isCancelled()) {
                return std::make_tuple(QImage(), true, ignoreColorProfile,
                                       request.token);
            }
            // Report which policy this decode used: a toggle that lands after we
            // pass the abort check above can't stop this worker, so the finish
            // handler must detect and discard the now-wrong-policy result.
            return std::make_tuple(image, false, ignoreColorProfile,
                                   request.token);
        });

        using ImageResult = std::tuple<QImage, bool, bool, quint64>;
        auto *watcher = new QFutureWatcher<ImageResult>(this);
        connect(watcher, &QFutureWatcher<ImageResult>::finished, this,
                [this, watcher, request]() {
                    const ImageResult result = watcher->result();
                    watcher->deleteLater();

                    const QImage image = std::get<0>(result);
                    const bool workerCancelled = std::get<1>(result);
                    const bool usedIgnoreColorProfile = std::get<2>(result);
                    const quint64 completedToken = std::get<3>(result);

                    // The full-image cache is keyed by path only, so a result
                    // produced under a since-changed color-profile policy would
                    // poison it (and reach the UI) until the next reload. Drop
                    // it like a cancellation; the reload triggered by the toggle
                    // re-queues a fresh decode under the current policy.
                    const bool staleProfile = usedIgnoreColorProfile !=
                        m_ignoreColorProfile.load(std::memory_order_acquire);
                    const bool drop = workerCancelled || staleProfile;

                    bool accepted = false;
                    bool notify = false;
                    {
                        QMutexLocker locker(&m_cacheMutex);
                        const auto activeIt = m_activeImageRequests.constFind(request.imagePath);
                        const bool ownsActive = activeIt != m_activeImageRequests.constEnd()
                            && activeIt.value() == completedToken;
                        auto pendingIt = m_pendingImageRequests.find(request.imagePath);
                        const bool tokenMatches = ownsActive &&
                            pendingIt != m_pendingImageRequests.end() &&
                            pendingIt->token == completedToken;
                        if (!drop && tokenMatches) {
                            accepted = true;
                            notify = pendingIt->notify;
                            m_pendingImageRequests.erase(pendingIt);
                        }
                        if (ownsActive) {
                            m_activeImageRequests.remove(request.imagePath);
                        }
                        m_activeImageLoads = qMax(0, m_activeImageLoads - 1);
                        if (request.priority < kPriorityVisible) {
                            m_activeImagePrefetchLoads = qMax(0,
                                m_activeImagePrefetchLoads - 1);
                        }
                        const auto currentIt = m_pendingImageRequests.constFind(request.imagePath);
                        if (ownsActive && currentIt != m_pendingImageRequests.constEnd()) {
                            enqueueImageRequestUnlocked(*currentIt);
                        }
                    }

                    finishImageRequest(request.imagePath,
                                       accepted ? image : QImage(),
                                       accepted,
                                       notify);
                    processImageQueue();
                });

        watcher->setFuture(future);
    }
}

void ImageLoader::finishImageRequest(const QString &imagePath,
                                     const QImage &image,
                                     bool accepted,
                                     bool notify)
{
    if (accepted && !image.isNull()) {
        QMutexLocker locker(&m_cacheMutex);
        const qint64 bytes = imageByteSize(image);
        ImageCacheEntry entry{imagePath, image, bytes};
        insertImageEntryUnlocked(std::move(entry));
        trimImageCache();
    }

    if (accepted && notify) {
        emit imageReady(imagePath, image);
    }
}

QImage ImageLoader::getCachedThumbnail(const QString &imagePath)
{
    // Honour the current color-profile policy: a thumbnail decoded under the
    // previous policy could linger in the path index (e.g. an in-flight worker
    // that finished after a toggle cleared the cache), and this size-agnostic
    // lookup would otherwise hand it back. The two-arg overload is already
    // policy-safe because it rebuilds the policy-tagged key.
    const bool ignoreColorProfile = m_ignoreColorProfile.load(std::memory_order_acquire);
    QMutexLocker locker(&m_cacheMutex);
    auto pathIt = m_thumbnailPathIndex.constFind(imagePath);
    if (pathIt == m_thumbnailPathIndex.constEnd()) {
        return QImage();
    }

    // Walk only the (small) list of cached sizes for this path.
    // Prefer the sharpest available decode; use quality as the tie-breaker.
    // A tiny HQ list thumbnail is a worse compare preview than a larger fast
    // decode of the same source.
    QImage best;
    int bestExtent = 0;
    bool bestHighQuality = false;
    ThumbnailIter bestIt = m_thumbnailLru.end();
    for (const QString &key : pathIt.value()) {
        auto it = m_thumbnailIndex.constFind(key);
        if (it == m_thumbnailIndex.constEnd()) {
            continue;
        }
        const CacheEntry &entry = *it.value();
        if (entry.ignoreColorProfile != ignoreColorProfile) {
            continue;
        }
        if (entry.thumbnail.isNull()) {
            continue;
        }
        const int extent = qMax(entry.requestedSize.width(),
                                entry.requestedSize.height());
        const bool hq = entry.highQuality;
        const bool better = best.isNull()
            || extent > bestExtent
            || (extent == bestExtent && hq && !bestHighQuality);
        if (better) {
            best = entry.thumbnail;
            bestExtent = extent;
            bestHighQuality = hq;
            bestIt = it.value();
        }
    }
    if (bestIt != m_thumbnailLru.end()) {
        touchThumbnailEntryUnlocked(bestIt);
    }
    return best;
}

QImage ImageLoader::getCachedThumbnail(const QString &imagePath, const QSize &thumbnailSize)
{
    const bool ignoreColorProfile = m_ignoreColorProfile.load(std::memory_order_acquire);
    QMutexLocker locker(&m_cacheMutex);
    const QString highQualityKey = memoryCacheKey(imagePath, thumbnailSize, true, ignoreColorProfile);
    auto it = m_thumbnailIndex.constFind(highQualityKey);
    if (it != m_thumbnailIndex.constEnd()) {
        touchThumbnailEntryUnlocked(it.value());
        return it.value()->thumbnail;
    }
    const QString fastKey = memoryCacheKey(imagePath, thumbnailSize, false, ignoreColorProfile);
    it = m_thumbnailIndex.constFind(fastKey);
    if (it != m_thumbnailIndex.constEnd()) {
        touchThumbnailEntryUnlocked(it.value());
        return it.value()->thumbnail;
    }
    return QImage();
}

QImage ImageLoader::getCachedImage(const QString &imagePath)
{
    QMutexLocker locker(&m_cacheMutex);
    auto it = m_imageIndex.constFind(imagePath);
    if (it != m_imageIndex.constEnd()) {
        touchImageEntryUnlocked(it.value());
        return it.value()->image;
    }
    return QImage();
}

void ImageLoader::clearCache()
{
    QMutexLocker locker(&m_cacheMutex);
    m_thumbnailLru.clear();
    m_thumbnailIndex.clear();
    m_thumbnailPathIndex.clear();
    m_imageLru.clear();
    m_imageIndex.clear();
    m_pendingRequests.clear();
    m_pendingImageRequests.clear();
    m_highPriorityQueue.clear();
    m_normalPriorityQueue.clear();
    m_backgroundPriorityQueue.clear();
    m_visibleImageQueue.clear();
    m_prefetchImageQueue.clear();
    m_currentThumbnailCacheBytes = 0;
    m_currentImageCacheBytes = 0;
}

void ImageLoader::setMaxCacheSize(int maxSize)
{
    m_maxCacheSize = qMax(1, maxSize);
    QMutexLocker locker(&m_cacheMutex);
    m_maxThumbnailCacheBytes = qMax<qint64>(1, static_cast<qint64>(m_maxCacheSize) * 512LL * 1024LL);
    trimCache();
}

void ImageLoader::setMaxConcurrentLoads(int maxConcurrentLoads)
{
    int desired = 0;
    {
        QMutexLocker locker(&m_cacheMutex);
        m_maxConcurrentLoads = qBound(1, maxConcurrentLoads, 32);
        desired = m_maxConcurrentLoads;
    }
    // Make sure the pool can actually run the requested number of loads
    // concurrently (it never shrinks below its configured floor).
    if (m_decodePool->maxThreadCount() < desired) {
        m_decodePool->setMaxThreadCount(desired);
    }
    processQueue();
}

void ImageLoader::cancelThumbnailRequestsExcept(const QSet<QString> &keepPaths)
{
    {
        QMutexLocker locker(&m_cacheMutex);

        auto filterQueue = [&keepPaths](QQueue<ThumbnailRequest> &queue) {
            QQueue<ThumbnailRequest> filtered;
            while (!queue.isEmpty()) {
                ThumbnailRequest req = queue.dequeue();
                if (keepPaths.contains(req.imagePath)) {
                    filtered.enqueue(req);
                }
            }
            queue = filtered;
        };

        filterQueue(m_highPriorityQueue);
        filterQueue(m_normalPriorityQueue);
        filterQueue(m_backgroundPriorityQueue);

        for (auto it = m_pendingRequests.begin(); it != m_pendingRequests.end();) {
            if (keepPaths.contains(it->imagePath)) {
                ++it;
            } else {
                const bool wasActive = m_activeThumbnailRequests.contains(it.key());
                it = m_pendingRequests.erase(it);
                // Active tokens are counted when their completion is rejected;
                // count only never-dispatched work here to avoid double metrics.
                if (!wasActive) {
                    ++m_metricCancelled;
                }
            }
        }
    }

    // Update the shared cancellation state. Workers that captured an
    // older generation observe the new keep set under cancelState->keepMutex.
    {
        QMutexLocker locker(&m_thumbnailCancel->keepMutex);
        m_thumbnailCancel->keepPaths = keepPaths;
    }
    // Release-ordered bump so workers that see the new generation also
    // see the new keep set on the next load.
    m_thumbnailCancel->generation.fetch_add(1, std::memory_order_release);
}

void ImageLoader::cancelAllThumbnailRequests()
{
    cancelThumbnailRequestsExcept({});
}

void ImageLoader::cancelImageRequestsExcept(const QSet<QString> &keepPaths)
{
    {
        QMutexLocker locker(&m_cacheMutex);

        auto filterQueue = [&keepPaths](QQueue<ImageRequest> &queue) {
            QQueue<ImageRequest> filtered;
            while (!queue.isEmpty()) {
                const ImageRequest request = queue.dequeue();
                if (keepPaths.contains(request.imagePath)) {
                    filtered.enqueue(request);
                }
            }
            queue = std::move(filtered);
        };
        filterQueue(m_visibleImageQueue);
        filterQueue(m_prefetchImageQueue);

        for (auto it = m_pendingImageRequests.begin(); it != m_pendingImageRequests.end();) {
            if (keepPaths.contains(it->imagePath)) {
                ++it;
            } else {
                it = m_pendingImageRequests.erase(it);
            }
        }
    }

    {
        QMutexLocker locker(&m_imageCancel->keepMutex);
        m_imageCancel->keepPaths = keepPaths;
    }
    m_imageCancel->generation.fetch_add(1, std::memory_order_release);
}

bool ImageLoader::ignoreColorProfile() const
{
    return m_ignoreColorProfile.load(std::memory_order_acquire);
}

void ImageLoader::setIgnoreColorProfile(bool enabled)
{
    const bool previous = m_ignoreColorProfile.exchange(enabled, std::memory_order_acq_rel);
    if (previous == enabled) {
        return;
    }

    // Drop all cached results — they were produced under the previous policy
    // and we don't want callers to receive an inconsistent mix of stripped
    // and tagged images. Pending requests stay queued; their snapshotted flag
    // is still consistent with their cache key, so they'll just land under
    // the (now-cleared) cache with the right key.
    {
        QMutexLocker locker(&m_cacheMutex);
        m_thumbnailLru.clear();
        m_thumbnailIndex.clear();
        m_thumbnailPathIndex.clear();
        m_imageLru.clear();
        m_imageIndex.clear();
        m_currentThumbnailCacheBytes = 0;
        m_currentImageCacheBytes = 0;

        // Drop old-policy thumbnail work as well. Request keys isolate the
        // resulting cache entry, but emitting an old-policy image to a model
        // after the toggle would still briefly display inconsistent pixels.
        m_pendingRequests.clear();
        m_highPriorityQueue.clear();
        m_normalPriorityQueue.clear();
        m_backgroundPriorityQueue.clear();

        // Drop queued/pending IMAGE requests: any in-flight worker finishes
        // under the old policy and is discarded by the stale-policy check, and
        // clearing the pending set lets the post-toggle reload re-queue fresh
        // decodes for the same paths (the dedup in requestImage would otherwise
        // suppress them).
        m_pendingImageRequests.clear();
        m_visibleImageQueue.clear();
        m_prefetchImageQueue.clear();
    }

    // Policy changes cancel old work unconditionally. Reusing the viewport keep
    // sets here would let an old-policy decode for a visible/current path run to
    // completion and block its fresh replacement behind the active-token guard.
    {
        QMutexLocker locker(&m_thumbnailCancel->keepMutex);
        m_thumbnailCancel->keepPaths.clear();
    }
    {
        QMutexLocker locker(&m_imageCancel->keepMutex);
        m_imageCancel->keepPaths.clear();
    }
    m_thumbnailCancel->generation.fetch_add(1, std::memory_order_release);
    m_imageCancel->generation.fetch_add(1, std::memory_order_release);
}

QHash<QString, qint64> ImageLoader::thumbnailMetrics() const
{
    qint64 pendingDiskWrites = 0;
    qint64 pendingDiskWriteBytes = 0;
    qint64 droppedDiskWrites = 0;
    {
        QMutexLocker locker(&m_diskWriteState->mutex);
        pendingDiskWrites = m_diskWriteState->pendingCount;
        pendingDiskWriteBytes = m_diskWriteState->pendingBytes;
        droppedDiskWrites = m_diskWriteState->droppedCount;
    }
    QMutexLocker locker(&m_cacheMutex);
    return {
        {QStringLiteral("requests"), m_metricRequests},
        {QStringLiteral("memoryHits"), m_metricMemoryHits},
        {QStringLiteral("diskHits"), m_metricDiskHits},
        {QStringLiteral("decodes"), m_metricDecodes},
        {QStringLiteral("cancelled"), m_metricCancelled},
        {QStringLiteral("thumbnailCacheBytes"), m_currentThumbnailCacheBytes},
        {QStringLiteral("imageCacheBytes"), m_currentImageCacheBytes},
        {QStringLiteral("pendingDiskWrites"), pendingDiskWrites},
        {QStringLiteral("pendingDiskWriteBytes"), pendingDiskWriteBytes},
        {QStringLiteral("droppedDiskWrites"), droppedDiskWrites}
    };
}

void ImageLoader::touchThumbnailEntryUnlocked(ThumbnailIter it)
{
    // std::list::splice within the same list moves the node in O(1) and
    // does NOT invalidate any iterators (the moved one included), so our
    // QHash<Key, iterator> index stays valid.
    if (it != m_thumbnailLru.begin()) {
        m_thumbnailLru.splice(m_thumbnailLru.begin(), m_thumbnailLru, it);
    }
}

void ImageLoader::insertThumbnailEntryUnlocked(CacheEntry &&entry)
{
    auto existing = m_thumbnailIndex.find(entry.key);
    if (existing != m_thumbnailIndex.end()) {
        ThumbnailIter listIt = existing.value();
        m_currentThumbnailCacheBytes -= listIt->byteSize;
        // Update in place and move to MRU; do not touch the path index
        // because the (path, key) mapping is unchanged.
        *listIt = std::move(entry);
        m_currentThumbnailCacheBytes += listIt->byteSize;
        touchThumbnailEntryUnlocked(listIt);
        return;
    }

    m_thumbnailLru.push_front(std::move(entry));
    ThumbnailIter listIt = m_thumbnailLru.begin();
    m_thumbnailIndex.insert(listIt->key, listIt);
    m_thumbnailPathIndex[listIt->imagePath].append(listIt->key);
    m_currentThumbnailCacheBytes += listIt->byteSize;
}

void ImageLoader::eraseThumbnailEntryUnlocked(ThumbnailIter it)
{
    m_currentThumbnailCacheBytes -= it->byteSize;
    m_thumbnailIndex.remove(it->key);

    auto pathIt = m_thumbnailPathIndex.find(it->imagePath);
    if (pathIt != m_thumbnailPathIndex.end()) {
        pathIt.value().removeOne(it->key);
        if (pathIt.value().isEmpty()) {
            m_thumbnailPathIndex.erase(pathIt);
        }
    }

    m_thumbnailLru.erase(it);
}

void ImageLoader::touchImageEntryUnlocked(ImageIter it)
{
    if (it != m_imageLru.begin()) {
        m_imageLru.splice(m_imageLru.begin(), m_imageLru, it);
    }
}

void ImageLoader::insertImageEntryUnlocked(ImageCacheEntry &&entry)
{
    auto existing = m_imageIndex.find(entry.imagePath);
    if (existing != m_imageIndex.end()) {
        ImageIter listIt = existing.value();
        m_currentImageCacheBytes -= listIt->byteSize;
        *listIt = std::move(entry);
        m_currentImageCacheBytes += listIt->byteSize;
        touchImageEntryUnlocked(listIt);
        return;
    }

    m_imageLru.push_front(std::move(entry));
    ImageIter listIt = m_imageLru.begin();
    m_imageIndex.insert(listIt->imagePath, listIt);
    m_currentImageCacheBytes += listIt->byteSize;
}

void ImageLoader::eraseImageEntryUnlocked(ImageIter it)
{
    m_currentImageCacheBytes -= it->byteSize;
    m_imageIndex.remove(it->imagePath);
    m_imageLru.erase(it);
}

void ImageLoader::trimCache()
{
    while (!m_thumbnailLru.empty() &&
           (static_cast<int>(m_thumbnailLru.size()) > m_maxCacheSize ||
            m_currentThumbnailCacheBytes > m_maxThumbnailCacheBytes)) {
        // LRU node is at the back; O(1) eviction.
        ThumbnailIter victim = std::prev(m_thumbnailLru.end());
        eraseThumbnailEntryUnlocked(victim);
    }
}

void ImageLoader::trimImageCache()
{
    while (!m_imageLru.empty() &&
           (static_cast<int>(m_imageLru.size()) > m_maxImageCacheSize ||
            m_currentImageCacheBytes > m_maxImageCacheBytes)) {
        ImageIter victim = std::prev(m_imageLru.end());
        eraseImageEntryUnlocked(victim);
    }
}

QString ImageLoader::memoryCacheKey(const QString &imagePath,
                                    const QSize &thumbnailSize,
                                    bool highQuality,
                                    bool ignoreColorProfile)
{
    return imagePath
        + QLatin1Char('|')
        + QString::number(thumbnailSize.width())
        + QLatin1Char('x')
        + QString::number(thumbnailSize.height())
        + QLatin1Char('|')
        + (highQuality ? QStringLiteral("hq") : QStringLiteral("fast"))
        + QLatin1Char('|')
        + (ignoreColorProfile ? QStringLiteral("noicc") : QStringLiteral("icc"));
}

QString ImageLoader::makeCacheKey(const QString &imagePath,
                                  const QSize &thumbnailSize,
                                  const QDateTime &lastModifiedUtc,
                                  bool highQuality,
                                  bool ignoreColorProfile)
{
    const QByteArray raw = imagePath.toUtf8()
        + '|'
        + QByteArray::number(thumbnailSize.width())
        + 'x'
        + QByteArray::number(thumbnailSize.height())
        + '|'
        + QByteArray::number(lastModifiedUtc.toMSecsSinceEpoch())
        + '|'
        + (highQuality ? "hq" : "fast")
        + '|'
        + (ignoreColorProfile ? "noicc" : "icc");

    return QString::fromLatin1(QCryptographicHash::hash(raw, QCryptographicHash::Sha1).toHex());
}

QString ImageLoader::cacheRootDir()
{
    static const QString root = []() {
        QStringList candidates;
        const QString standardCache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        if (!standardCache.isEmpty()) {
            candidates.append(standardCache);
        }
        candidates.append(QDir::tempPath() + QStringLiteral("/ImageCompareCache"));

        for (const QString &base : candidates) {
            QDir dir(base);
            if (!dir.mkpath(QStringLiteral("thumbnails"))) {
                continue;
            }

            const QString thumbnailDir = dir.filePath(QStringLiteral("thumbnails"));
            const QFileInfo info(thumbnailDir);
            if (info.isDir() && info.isWritable()) {
                return thumbnailDir;
            }
        }

        return QDir::tempPath();
    }();
    return root;
}

QString ImageLoader::cachePathForKey(const QString &cacheKey)
{
    return QDir(cacheRootDir()).filePath(cacheKey + QStringLiteral(".png"));
}

void ImageLoader::pruneDiskCache(qint64 byteBudget, bool respectMaintenanceStamp)
{
    const QDir dir(cacheRootDir());
    const QString stampPath = dir.filePath(QStringLiteral(".maintenance"));
    const QFileInfo stampInfo(stampPath);
    if (respectMaintenanceStamp && stampInfo.exists() &&
        stampInfo.lastModified().toUTC().secsTo(QDateTime::currentDateTimeUtc()) < 24 * 60 * 60) {
        return;
    }

    QFileInfoList entries = dir.entryInfoList({QStringLiteral("*.png")},
                                              QDir::Files | QDir::NoSymLinks,
                                              QDir::Time | QDir::Reversed);
    qint64 totalBytes = 0;
    const QDateTime expiry = QDateTime::currentDateTimeUtc().addDays(-90);
    for (const QFileInfo &entry : std::as_const(entries)) {
        if (entry.lastModified().toUTC() < expiry) {
            QFile::remove(entry.absoluteFilePath());
            continue;
        }
        totalBytes += entry.size();
    }

    if (totalBytes > byteBudget) {
        // entries are oldest first. Remove until the retained cache fits its hard
        // budget; cache misses remain correctness-neutral and regenerate lazily.
        for (const QFileInfo &entry : std::as_const(entries)) {
            if (totalBytes <= byteBudget) {
                break;
            }
            if (!entry.exists()) {
                continue;
            }
            const qint64 bytes = entry.size();
            if (QFile::remove(entry.absoluteFilePath())) {
                totalBytes -= bytes;
            }
        }
    }

    QFile stamp(stampPath);
    if (stamp.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        stamp.write(QByteArray::number(QDateTime::currentMSecsSinceEpoch()));
    }
}

QDateTime ImageLoader::sourceLastModifiedUtc(const QString &imagePath)
{
    return QFileInfo(imagePath).lastModified().toUTC();
}

QImage ImageLoader::tryLoadDiskCachedThumbnail(const QString &imagePath,
                                               const QSize &thumbnailSize,
                                               const QDateTime &lastModifiedUtc,
                                               bool highQuality,
                                               bool ignoreColorProfile)
{
    if (!lastModifiedUtc.isValid()) {
        return QImage();
    }

    const QString cacheKey = makeCacheKey(imagePath, thumbnailSize, lastModifiedUtc, highQuality,
                                          ignoreColorProfile);
    const QString cachePath = cachePathForKey(cacheKey);
    QImage cached(cachePath);
    if (cached.isNull()) {
        return QImage();
    }
    return cached;
}

void ImageLoader::persistDiskThumbnail(const QString &imagePath,
                                       const QSize &thumbnailSize,
                                       const QDateTime &lastModifiedUtc,
                                       const QImage &thumbnail,
                                       bool highQuality,
                                       bool ignoreColorProfile)
{
    if (thumbnail.isNull() || !lastModifiedUtc.isValid()) {
        return;
    }

    const QString cacheKey = makeCacheKey(imagePath, thumbnailSize, lastModifiedUtc, highQuality,
                                          ignoreColorProfile);
    const QString path = cachePathForKey(cacheKey);
    QFileInfo fi(path);
    QDir().mkpath(fi.absolutePath());

    // Write atomically via QSaveFile (temp file + rename on commit) so a reader
    // — or another decode worker — can never observe a half-written PNG, which
    // a plain QImage::save(path) leaves behind if the process dies (or a second
    // writer interleaves) mid-write. A torn file would otherwise decode to null
    // and force a needless re-decode.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }
    if (!thumbnail.save(&file, "PNG")) {
        file.cancelWriting();
        return;
    }
    file.commit();
}

void ImageLoader::scheduleDiskThumbnailWrite(const QString &imagePath,
                                             const QSize &thumbnailSize,
                                             const QDateTime &lastModifiedUtc,
                                             const QImage &thumbnail,
                                             bool highQuality,
                                             bool ignoreColorProfile)
{
    if (thumbnail.isNull() || !lastModifiedUtc.isValid()) {
        return;
    }

    const QString cacheKey = makeCacheKey(imagePath, thumbnailSize, lastModifiedUtc,
                                          highQuality, ignoreColorProfile);
    const qint64 bytes = imageByteSize(thumbnail);
    const auto state = m_diskWriteState;
    {
        QMutexLocker locker(&state->mutex);
        if (state->pendingKeys.contains(cacheKey)) {
            return;
        }
        if (bytes > kMaxPendingDiskWriteBytes ||
            state->pendingCount >= kMaxPendingDiskWrites ||
            state->pendingBytes + bytes > kMaxPendingDiskWriteBytes) {
            ++state->droppedCount;
            return;
        }
        state->pendingKeys.insert(cacheKey);
        ++state->pendingCount;
        state->pendingBytes += bytes;
    }

    const QFuture<void> future = QtConcurrent::run(
        m_diskCachePool.get(),
        [imagePath, thumbnailSize, lastModifiedUtc, thumbnail, cacheKey, bytes,
         highQuality, ignoreColorProfile, state]() {
            persistDiskThumbnail(imagePath, thumbnailSize, lastModifiedUtc,
                                 thumbnail, highQuality, ignoreColorProfile);
            {
                QMutexLocker locker(&state->mutex);
                state->pendingKeys.remove(cacheKey);
                state->pendingCount = qMax(0, state->pendingCount - 1);
                state->pendingBytes = qMax<qint64>(0, state->pendingBytes - bytes);
            }

            // Startup maintenance is throttled to once per day, but a long-lived
            // process still needs periodic enforcement. Normal 200px thumbnails
            // trigger an inexpensive background sweep every 128 writes; unusually
            // large preview entries trigger one immediately.
            static std::atomic<quint64> writesSincePrune{0};
            const quint64 completed = writesSincePrune.fetch_add(
                1, std::memory_order_relaxed) + 1;
            if ((completed % 128) == 0 || bytes >= 2LL * 1024LL * 1024LL) {
                pruneDiskCache(kDiskCacheBudgetBytes, false);
            }
        });
    Q_UNUSED(future);
}

qint64 ImageLoader::imageByteSize(const QImage &image)
{
    return image.isNull() ? 0 : static_cast<qint64>(image.sizeInBytes());
}
