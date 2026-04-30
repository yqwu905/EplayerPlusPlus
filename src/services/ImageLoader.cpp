#include "ImageLoader.h"
#include "utils/ImageUtils.h"

#include <QtConcurrent>
#include <QMutexLocker>
#include <QFutureWatcher>
#include <QMetaObject>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QCryptographicHash>

#include <tuple>

namespace
{
constexpr int kPriorityBackground = 0;
constexpr int kPriorityPrefetch = 1;
constexpr int kPriorityVisible = 2;
}

ImageLoader::ImageLoader(QObject *parent)
    : QObject(parent)
{
}

ImageLoader::~ImageLoader() = default;

void ImageLoader::requestThumbnail(const QString &imagePath, const QSize &thumbnailSize)
{
    enqueueThumbnailRequest(imagePath, thumbnailSize, kPriorityVisible, false);
}

void ImageLoader::requestThumbnailBatch(const QStringList &imagePaths, const QSize &thumbnailSize)
{
    for (const QString &path : imagePaths) {
        enqueueThumbnailRequest(path, thumbnailSize, kPriorityBackground, true);
    }
}

void ImageLoader::requestThumbnailBatchVisibleFirst(const QStringList &imagePaths, const QSize &thumbnailSize)
{
    for (const QString &path : imagePaths) {
        enqueueThumbnailRequest(path, thumbnailSize, kPriorityVisible, false);
    }
}

void ImageLoader::enqueueThumbnailRequest(const QString &imagePath,
                                          const QSize &thumbnailSize,
                                          int priority,
                                          bool highQuality)
{
    if (imagePath.isEmpty() || !thumbnailSize.isValid()) {
        return;
    }

    QImage cachedThumbnail;
    bool hasCached = false;
    bool shouldProcess = false;

    const QString key = memoryCacheKey(imagePath, thumbnailSize, highQuality);
    const QString highQualityKey = memoryCacheKey(imagePath, thumbnailSize, true);
    const QString fastKey = memoryCacheKey(imagePath, thumbnailSize, false);

    {
        QMutexLocker locker(&m_cacheMutex);
        ++m_metricRequests;

        auto cacheIt = m_thumbnailCache.constFind(highQualityKey);
        if (cacheIt == m_thumbnailCache.constEnd() && !highQuality) {
            cacheIt = m_thumbnailCache.constFind(fastKey);
        }
        if (cacheIt != m_thumbnailCache.constEnd()) {
            cachedThumbnail = cacheIt->thumbnail;
            hasCached = !cachedThumbnail.isNull();
            if (hasCached) {
                ++m_metricMemoryHits;
            }
        }

        if (!hasCached) {
            ThumbnailRequest request{key, imagePath, thumbnailSize, priority, highQuality};
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
        }

        auto future = QtConcurrent::run([request]() {
            const QDateTime modifiedUtc = sourceLastModifiedUtc(request.imagePath);

            if (QImage disk = tryLoadDiskCachedThumbnail(
                    request.imagePath,
                    request.thumbnailSize,
                    modifiedUtc,
                    request.highQuality);
                !disk.isNull()) {
                return std::make_tuple(request.key,
                                       request.imagePath,
                                       request.thumbnailSize,
                                       disk,
                                       modifiedUtc,
                                       request.highQuality,
                                       true);
            }

            const Qt::TransformationMode mode = request.highQuality
                ? Qt::SmoothTransformation
                : Qt::FastTransformation;
            QImage generated = ImageUtils::generateThumbnail(
                request.imagePath, request.thumbnailSize, mode);

            if (!generated.isNull()) {
                persistDiskThumbnail(request.imagePath,
                                     request.thumbnailSize,
                                     modifiedUtc,
                                     generated,
                                     request.highQuality);
            }

            return std::make_tuple(request.key,
                                   request.imagePath,
                                   request.thumbnailSize,
                                   generated,
                                   modifiedUtc,
                                   request.highQuality,
                                   false);
        });

        auto *watcher =
            new QFutureWatcher<std::tuple<QString, QString, QSize, QImage, QDateTime, bool, bool>>(this);
        connect(watcher,
                &QFutureWatcher<std::tuple<QString, QString, QSize, QImage, QDateTime, bool, bool>>::finished,
                this,
                [this, watcher]() {
                    auto [key, imagePath, thumbnailSize, thumbnail, modifiedUtc, highQuality, fromDisk] =
                        watcher->result();
                    watcher->deleteLater();

                    bool cancelledBeforeEmit = false;
                    {
                        QMutexLocker locker(&m_cacheMutex);
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

                        m_activeLoads = qMax(0, m_activeLoads - 1);
                    }

                    finishRequest(imagePath,
                                  thumbnailSize,
                                  thumbnail,
                                  modifiedUtc,
                                  highQuality,
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
                                bool cancelledBeforeEmit)
{
    if (!thumbnail.isNull()) {
        QMutexLocker locker(&m_cacheMutex);
        const QString key = memoryCacheKey(imagePath, thumbnailSize, highQuality);
        const qint64 bytes = imageByteSize(thumbnail);
        auto oldIt = m_thumbnailCache.find(key);
        if (oldIt != m_thumbnailCache.end()) {
            m_currentThumbnailCacheBytes -= oldIt->byteSize;
        }
        m_thumbnailCache.insert(key,
                                {imagePath,
                                 thumbnail,
                                 thumbnailSize,
                                 lastModifiedUtc,
                                 highQuality,
                                 ++m_cacheSequenceCounter,
                                 bytes});
        m_currentThumbnailCacheBytes += bytes;
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
        auto it = m_imageCache.find(imagePath);
        if (it != m_imageCache.end()) {
            cachedImage = it->image;
            hasCached = !cachedImage.isNull();
            if (hasCached) {
                it->sequence = ++m_imageCacheSequenceCounter;
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
        }

        auto future = QtConcurrent::run([imagePath]() {
            return QImage(imagePath);
        });

        auto *watcher = new QFutureWatcher<QImage>(this);
        connect(watcher, &QFutureWatcher<QImage>::finished, this,
                [this, watcher, imagePath]() {
                    QImage image = watcher->result();
                    watcher->deleteLater();

                    bool cancelledBeforeEmit = false;
                    {
                        QMutexLocker locker(&m_cacheMutex);
                        if (m_pendingImageRequests.contains(imagePath)) {
                            m_pendingImageRequests.remove(imagePath);
                        } else {
                            cancelledBeforeEmit = true;
                        }
                        m_activeImageLoads = qMax(0, m_activeImageLoads - 1);
                    }

                    finishImageRequest(imagePath, image, cancelledBeforeEmit);
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
        auto oldIt = m_imageCache.find(imagePath);
        if (oldIt != m_imageCache.end()) {
            m_currentImageCacheBytes -= oldIt->byteSize;
        }
        m_imageCache.insert(imagePath,
                            {image,
                             ++m_imageCacheSequenceCounter,
                             bytes});
        m_currentImageCacheBytes += bytes;
        trimImageCache();
    }

    if (!cancelledBeforeEmit) {
        emit imageReady(imagePath, image);
    }
}

QImage ImageLoader::getCachedThumbnail(const QString &imagePath) const
{
    QMutexLocker locker(&m_cacheMutex);
    CacheEntry best;
    bool hasBest = false;
    for (auto it = m_thumbnailCache.constBegin(); it != m_thumbnailCache.constEnd(); ++it) {
        if (it->imagePath != imagePath || it->thumbnail.isNull()) {
            continue;
        }
        if (!hasBest || (it->highQuality && !best.highQuality) ||
            imageByteSize(it->thumbnail) > imageByteSize(best.thumbnail)) {
            best = *it;
            hasBest = true;
        }
    }
    return hasBest ? best.thumbnail : QImage();
}

QImage ImageLoader::getCachedThumbnail(const QString &imagePath, const QSize &thumbnailSize) const
{
    QMutexLocker locker(&m_cacheMutex);
    const QString highQualityKey = memoryCacheKey(imagePath, thumbnailSize, true);
    auto it = m_thumbnailCache.constFind(highQualityKey);
    if (it != m_thumbnailCache.constEnd()) {
        return it->thumbnail;
    }
    const QString fastKey = memoryCacheKey(imagePath, thumbnailSize, false);
    it = m_thumbnailCache.constFind(fastKey);
    if (it != m_thumbnailCache.constEnd()) {
        return it->thumbnail;
    }
    return QImage();
}

QImage ImageLoader::getCachedImage(const QString &imagePath) const
{
    QMutexLocker locker(&m_cacheMutex);
    auto it = m_imageCache.constFind(imagePath);
    if (it != m_imageCache.constEnd()) {
        return it->image;
    }
    return QImage();
}

void ImageLoader::clearCache()
{
    QMutexLocker locker(&m_cacheMutex);
    m_thumbnailCache.clear();
    m_imageCache.clear();
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
    {
        QMutexLocker locker(&m_cacheMutex);
        m_maxConcurrentLoads = qBound(1, maxConcurrentLoads, 32);
    }
    processQueue();
}

void ImageLoader::cancelThumbnailRequestsExcept(const QSet<QString> &keepPaths)
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

void ImageLoader::cancelAllThumbnailRequests()
{
    cancelThumbnailRequestsExcept({});
}

void ImageLoader::cancelImageRequestsExcept(const QSet<QString> &keepPaths)
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

void ImageLoader::trimCache()
{
    while (m_thumbnailCache.size() > m_maxCacheSize ||
           m_currentThumbnailCacheBytes > m_maxThumbnailCacheBytes) {
        auto oldestIt = m_thumbnailCache.begin();
        for (auto it = m_thumbnailCache.begin(); it != m_thumbnailCache.end(); ++it) {
            if (it->sequence < oldestIt->sequence) {
                oldestIt = it;
            }
        }
        m_currentThumbnailCacheBytes -= oldestIt->byteSize;
        m_thumbnailCache.erase(oldestIt);
    }
}

void ImageLoader::trimImageCache()
{
    while (m_imageCache.size() > m_maxImageCacheSize ||
           m_currentImageCacheBytes > m_maxImageCacheBytes) {
        auto oldestIt = m_imageCache.begin();
        for (auto it = m_imageCache.begin(); it != m_imageCache.end(); ++it) {
            if (it->sequence < oldestIt->sequence) {
                oldestIt = it;
            }
        }
        m_currentImageCacheBytes -= oldestIt->byteSize;
        m_imageCache.erase(oldestIt);
    }
}

QString ImageLoader::memoryCacheKey(const QString &imagePath,
                                    const QSize &thumbnailSize,
                                    bool highQuality)
{
    return imagePath
        + QLatin1Char('|')
        + QString::number(thumbnailSize.width())
        + QLatin1Char('x')
        + QString::number(thumbnailSize.height())
        + QLatin1Char('|')
        + (highQuality ? QStringLiteral("hq") : QStringLiteral("fast"));
}

QString ImageLoader::makeCacheKey(const QString &imagePath,
                                  const QSize &thumbnailSize,
                                  const QDateTime &lastModifiedUtc,
                                  bool highQuality)
{
    const QByteArray raw = imagePath.toUtf8()
        + '|'
        + QByteArray::number(thumbnailSize.width())
        + 'x'
        + QByteArray::number(thumbnailSize.height())
        + '|'
        + QByteArray::number(lastModifiedUtc.toMSecsSinceEpoch())
        + '|'
        + (highQuality ? "hq" : "fast");

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
                                               bool highQuality)
{
    if (!lastModifiedUtc.isValid()) {
        return QImage();
    }

    const QString cacheKey = makeCacheKey(imagePath, thumbnailSize, lastModifiedUtc, highQuality);
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
                                       bool highQuality)
{
    if (thumbnail.isNull() || !lastModifiedUtc.isValid()) {
        return;
    }

    const QString cacheKey = makeCacheKey(imagePath, thumbnailSize, lastModifiedUtc, highQuality);
    const QString path = cachePathForKey(cacheKey);
    QFileInfo fi(path);
    QDir().mkpath(fi.absolutePath());
    thumbnail.save(path, "PNG");
}

qint64 ImageLoader::imageByteSize(const QImage &image)
{
    return image.isNull() ? 0 : static_cast<qint64>(image.sizeInBytes());
}
