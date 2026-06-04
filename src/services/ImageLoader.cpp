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

#include <tuple>
#include <utility>

namespace
{
constexpr int kPriorityBackground = 0;
constexpr int kPriorityPrefetch = 1;
constexpr int kPriorityVisible = 2;
}

ImageLoader::ImageLoader(QObject *parent)
    : QObject(parent),
      m_thumbnailCancel(std::make_shared<CancellationState>()),
      m_imageCancel(std::make_shared<CancellationState>())
{
    // More in-flight loads than CPU cores so network/slow-disk reads overlap
    // their I/O latency. Bounded so we don't flood a share with connections.
    m_decodePool.setMaxThreadCount(qBound(4, QThread::idealThreadCount() * 2, 12));
    m_decodePool.setExpiryTimeout(30000);
}

ImageLoader::~ImageLoader()
{
    // Bump generations so any worker still in its pre-decode check self-aborts.
    m_thumbnailCancel->generation.fetch_add(1, std::memory_order_release);
    m_imageCancel->generation.fetch_add(1, std::memory_order_release);

    // Drain the pool while `this` is still alive. Workers capture only a
    // shared_ptr to the cancellation state (never `this`), so joining them is
    // safe; clear() drops queued-but-unstarted runnables first.
    m_decodePool.clear();
    m_decodePool.waitForDone();

    // The QFutureWatcher children's finished-slots capture `this`. Their futures
    // are now finished, but the queued slot invocations have not run (we're on
    // the main thread). Delete the watchers now so those invocations never fire
    // on a half-destroyed object.
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
                touchThumbnailEntryUnlocked(listIt);
                ++m_metricMemoryHits;
            }
        }

        if (!hasCached) {
            ThumbnailRequest request{key, imagePath, thumbnailSize, priority, highQuality,
                                     ignoreColorProfile, sourceModifiedUtc};
            auto pendingIt = m_pendingRequests.find(key);
            if (pendingIt != m_pendingRequests.end()) {
                if (priority > pendingIt->priority) {
                    pendingIt->priority = priority;
                    enqueueRequestUnlocked(*pendingIt);
                    shouldProcess = true;
                }
            } else {
                m_pendingRequests.insert(key, request);
                enqueueRequestUnlocked(request);
                shouldProcess = true;
            }
        }
    }

    if (hasCached) {
        QMetaObject::invokeMethod(this, [this, imagePath, cachedThumbnail]() {
            emit thumbnailReady(imagePath, cachedThumbnail);
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
                    if (pendingIt->priority != candidate.priority) {
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
            capturedGeneration = m_thumbnailCancel->generation.load(std::memory_order_acquire);
        }

        // The worker captures a shared_ptr to the cancellation state so it
        // stays valid even if ImageLoader is destroyed mid-decode. Before
        // the expensive decode it consults the live generation / keep-set
        // and self-aborts so fast folder switches don't burn CPU.
        auto cancelState = m_thumbnailCancel;

        auto future = QtConcurrent::run(&m_decodePool, [request, cancelState, capturedGeneration]() {
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
                                       cancelled);
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

            if (!generated.isNull()) {
                persistDiskThumbnail(request.imagePath,
                                     request.thumbnailSize,
                                     modifiedUtc,
                                     generated,
                                     request.highQuality,
                                     request.ignoreColorProfile);
            }

            return makeResult(generated, modifiedUtc, false, false);
        });

        using ResultTuple = std::tuple<QString, QString, QSize, QImage, QDateTime, bool, bool, bool, bool>;
        auto *watcher = new QFutureWatcher<ResultTuple>(this);
        connect(watcher, &QFutureWatcher<ResultTuple>::finished, this,
                [this, watcher]() {
                    auto [key, imagePath, thumbnailSize, thumbnail, modifiedUtc,
                          highQuality, ignoreColorProfile, fromDisk, workerCancelled] = watcher->result();
                    watcher->deleteLater();

                    bool cancelledBeforeEmit = false;
                    {
                        QMutexLocker locker(&m_cacheMutex);
                        if (workerCancelled) {
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

                            if (m_pendingRequests.contains(key)) {
                                m_pendingRequests.remove(key);
                            } else {
                                cancelledBeforeEmit = true;
                                ++m_metricCancelled;
                            }
                        }

                        m_activeLoads = qMax(0, m_activeLoads - 1);
                    }

                    finishRequest(imagePath,
                                  thumbnailSize,
                                  workerCancelled ? QImage() : thumbnail,
                                  modifiedUtc,
                                  highQuality,
                                  ignoreColorProfile,
                                  cancelledBeforeEmit);
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
    if (!thumbnail.isNull()) {
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
    }
}

void ImageLoader::requestImage(const QString &imagePath)
{
    if (imagePath.isEmpty()) {
        return;
    }

    QImage cachedImage;
    bool hasCached = false;
    bool shouldProcess = false;
    {
        QMutexLocker locker(&m_cacheMutex);
        auto it = m_imageIndex.find(imagePath);
        if (it != m_imageIndex.end()) {
            ImageIter listIt = it.value();
            cachedImage = listIt->image;
            hasCached = !cachedImage.isNull();
            if (hasCached) {
                touchImageEntryUnlocked(listIt);
            }
        }

        if (!hasCached && !m_pendingImageRequests.contains(imagePath)) {
            m_pendingImageRequests.insert(imagePath);
            m_imageQueue.enqueue(imagePath);
            shouldProcess = true;
        }
    }

    if (hasCached) {
        QMetaObject::invokeMethod(this, [this, imagePath, cachedImage]() {
            emit imageReady(imagePath, cachedImage);
        }, Qt::QueuedConnection);
        return;
    }

    if (shouldProcess) {
        processImageQueue();
    }
}

void ImageLoader::requestImageBatch(const QStringList &imagePaths)
{
    for (const QString &path : imagePaths) {
        requestImage(path);
    }
}

void ImageLoader::processImageQueue()
{
    while (true) {
        QString imagePath;
        int capturedGeneration = 0;

        {
            QMutexLocker locker(&m_cacheMutex);
            if (m_activeImageLoads >= m_maxConcurrentImageLoads) {
                return;
            }

            while (!m_imageQueue.isEmpty()) {
                const QString candidate = m_imageQueue.dequeue();
                if (m_pendingImageRequests.contains(candidate)) {
                    imagePath = candidate;
                    break;
                }
            }

            if (imagePath.isEmpty()) {
                return;
            }

            ++m_activeImageLoads;
            capturedGeneration = m_imageCancel->generation.load(std::memory_order_acquire);
        }

        auto cancelState = m_imageCancel;
        // Snapshot the strip-profile flag for this load so a runtime toggle
        // can't make the in-flight decode disagree with the user's intent.
        const bool ignoreColorProfile = m_ignoreColorProfile.load(std::memory_order_acquire);
        auto future = QtConcurrent::run(&m_decodePool, [imagePath, cancelState, capturedGeneration, ignoreColorProfile]() {
            if (capturedGeneration !=
                cancelState->generation.load(std::memory_order_acquire)) {
                bool keep = false;
                {
                    QMutexLocker locker(&cancelState->keepMutex);
                    keep = cancelState->keepPaths.contains(imagePath);
                }
                if (!keep) {
                    return std::make_tuple(QImage(), true, ignoreColorProfile);
                }
            }
            QImage image(imagePath);
            if (ignoreColorProfile) {
                ImageUtils::stripColorProfile(image);
            }
            // Report which policy this decode used: a toggle that lands after we
            // pass the abort check above can't stop this worker, so the finish
            // handler must detect and discard the now-wrong-policy result.
            return std::make_tuple(image, false, ignoreColorProfile);
        });

        using ImageResult = std::tuple<QImage, bool, bool>;
        auto *watcher = new QFutureWatcher<ImageResult>(this);
        connect(watcher, &QFutureWatcher<ImageResult>::finished, this,
                [this, watcher, imagePath]() {
                    const ImageResult result = watcher->result();
                    watcher->deleteLater();

                    const QImage image = std::get<0>(result);
                    const bool workerCancelled = std::get<1>(result);
                    const bool usedIgnoreColorProfile = std::get<2>(result);

                    // The full-image cache is keyed by path only, so a result
                    // produced under a since-changed color-profile policy would
                    // poison it (and reach the UI) until the next reload. Drop
                    // it like a cancellation; the reload triggered by the toggle
                    // re-queues a fresh decode under the current policy.
                    const bool staleProfile = usedIgnoreColorProfile !=
                        m_ignoreColorProfile.load(std::memory_order_acquire);
                    const bool drop = workerCancelled || staleProfile;

                    bool cancelledBeforeEmit = false;
                    {
                        QMutexLocker locker(&m_cacheMutex);
                        if (drop) {
                            // Self-aborted or stale-policy; leave
                            // m_pendingImageRequests alone so a re-issued request
                            // can still be serviced by a fresh worker.
                            cancelledBeforeEmit = true;
                        } else if (m_pendingImageRequests.contains(imagePath)) {
                            m_pendingImageRequests.remove(imagePath);
                        } else {
                            cancelledBeforeEmit = true;
                        }
                        m_activeImageLoads = qMax(0, m_activeImageLoads - 1);
                    }

                    finishImageRequest(imagePath,
                                       drop ? QImage() : image,
                                       cancelledBeforeEmit);
                    processImageQueue();
                });

        watcher->setFuture(future);
    }
}

void ImageLoader::finishImageRequest(const QString &imagePath,
                                     const QImage &image,
                                     bool cancelledBeforeEmit)
{
    if (!image.isNull()) {
        QMutexLocker locker(&m_cacheMutex);
        const qint64 bytes = imageByteSize(image);
        ImageCacheEntry entry{imagePath, image, bytes};
        insertImageEntryUnlocked(std::move(entry));
        trimImageCache();
    }

    if (!cancelledBeforeEmit) {
        emit imageReady(imagePath, image);
    }
}

QImage ImageLoader::getCachedThumbnail(const QString &imagePath) const
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
    // Prefer high-quality over fast, then largest byte size.
    QImage best;
    qint64 bestBytes = 0;
    bool bestHighQuality = false;
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
        const qint64 bytes = entry.byteSize;
        const bool hq = entry.highQuality;
        const bool better = best.isNull()
            || (hq && !bestHighQuality)
            || (hq == bestHighQuality && bytes > bestBytes);
        if (better) {
            best = entry.thumbnail;
            bestBytes = bytes;
            bestHighQuality = hq;
        }
    }
    return best;
}

QImage ImageLoader::getCachedThumbnail(const QString &imagePath, const QSize &thumbnailSize) const
{
    const bool ignoreColorProfile = m_ignoreColorProfile.load(std::memory_order_acquire);
    QMutexLocker locker(&m_cacheMutex);
    const QString highQualityKey = memoryCacheKey(imagePath, thumbnailSize, true, ignoreColorProfile);
    auto it = m_thumbnailIndex.constFind(highQualityKey);
    if (it != m_thumbnailIndex.constEnd()) {
        return it.value()->thumbnail;
    }
    const QString fastKey = memoryCacheKey(imagePath, thumbnailSize, false, ignoreColorProfile);
    it = m_thumbnailIndex.constFind(fastKey);
    if (it != m_thumbnailIndex.constEnd()) {
        return it.value()->thumbnail;
    }
    return QImage();
}

QImage ImageLoader::getCachedImage(const QString &imagePath) const
{
    QMutexLocker locker(&m_cacheMutex);
    auto it = m_imageIndex.constFind(imagePath);
    if (it != m_imageIndex.constEnd()) {
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
    m_imageQueue.clear();
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
    if (m_decodePool.maxThreadCount() < desired) {
        m_decodePool.setMaxThreadCount(desired);
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
                it = m_pendingRequests.erase(it);
                ++m_metricCancelled;
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

        QQueue<QString> filtered;
        while (!m_imageQueue.isEmpty()) {
            const QString path = m_imageQueue.dequeue();
            if (keepPaths.contains(path)) {
                filtered.enqueue(path);
            }
        }
        m_imageQueue = filtered;

        for (auto it = m_pendingImageRequests.begin(); it != m_pendingImageRequests.end();) {
            if (keepPaths.contains(*it)) {
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

        // Drop queued/pending IMAGE requests: any in-flight worker finishes
        // under the old policy and is discarded by the stale-policy check, and
        // clearing the pending set lets the post-toggle reload re-queue fresh
        // decodes for the same paths (the dedup in requestImage would otherwise
        // suppress them). Thumbnail requests need no such reset — their cache
        // key already encodes the policy, so a re-request uses a fresh key and
        // never collides with the stale in-flight entry.
        m_pendingImageRequests.clear();
        m_imageQueue.clear();
    }

    // Bump cancel generations so any in-flight worker that started under the
    // old policy and is not on the keep path can self-abort before doing
    // pointless work — its output would be discarded by the cache anyway.
    m_thumbnailCancel->generation.fetch_add(1, std::memory_order_release);
    m_imageCancel->generation.fetch_add(1, std::memory_order_release);
}

QHash<QString, qint64> ImageLoader::thumbnailMetrics() const
{
    QMutexLocker locker(&m_cacheMutex);
    return {
        {QStringLiteral("requests"), m_metricRequests},
        {QStringLiteral("memoryHits"), m_metricMemoryHits},
        {QStringLiteral("diskHits"), m_metricDiskHits},
        {QStringLiteral("decodes"), m_metricDecodes},
        {QStringLiteral("cancelled"), m_metricCancelled},
        {QStringLiteral("thumbnailCacheBytes"), m_currentThumbnailCacheBytes},
        {QStringLiteral("imageCacheBytes"), m_currentImageCacheBytes}
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

qint64 ImageLoader::imageByteSize(const QImage &image)
{
    return image.isNull() ? 0 : static_cast<qint64>(image.sizeInBytes());
}
