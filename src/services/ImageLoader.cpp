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
#include <QThread>

namespace {
constexpr int kDefaultDecodeThreadCap = 6;
}

ImageLoader::ImageLoader(QObject *parent)
    : QObject(parent)
{
    const int logicalCores = qMax(1, QThread::idealThreadCount());
    const int estimatedPhysicalCores = qMax(1, logicalCores / 2);
    m_maxConcurrentLoads = qMin(estimatedPhysicalCores, kDefaultDecodeThreadCap);
}

ImageLoader::~ImageLoader() = default;

void ImageLoader::requestThumbnail(const QString &imagePath, const QSize &thumbnailSize)
{
    enqueueThumbnailRequest(imagePath, thumbnailSize, true, false);
}

void ImageLoader::requestThumbnailBatch(const QStringList &imagePaths, const QSize &thumbnailSize)
{
    for (const QString &path : imagePaths) {
        enqueueThumbnailRequest(path, thumbnailSize, false, true);
    }
}

void ImageLoader::requestThumbnailBatchVisibleFirst(const QStringList &imagePaths, const QSize &thumbnailSize)
{
    for (const QString &path : imagePaths) {
        enqueueThumbnailRequest(path, thumbnailSize, true, false);
    }
}

void ImageLoader::enqueueThumbnailRequest(const QString &imagePath,
                                          const QSize &thumbnailSize,
                                          bool highPriority,
                                          bool highQuality)
{
    if (imagePath.isEmpty() || !thumbnailSize.isValid()) {
        return;
    }

    QImage cachedThumbnail;
    bool hasCached = false;

    {
        QMutexLocker locker(&m_cacheMutex);
        ++m_metricRequests;

        const QString requestKey = makeRequestKey(imagePath, thumbnailSize, QColorSpace::SRgb);
        auto it = m_thumbnailCache.constFind(requestKey);
        if (it != m_thumbnailCache.constEnd() &&
            it->requestedSize == thumbnailSize &&
            it->highQuality >= highQuality) {
            cachedThumbnail = it->thumbnail;
            hasCached = !cachedThumbnail.isNull();
            if (hasCached) {
                ++m_metricMemoryHits;
            }
        }

        if (!hasCached) {
            auto pendingIt = m_pendingRequests.find(requestKey);
            if (pendingIt != m_pendingRequests.end()) {
                if (highPriority && !pendingIt->highPriority) {
                    pendingIt->highPriority = true;
                    pendingIt->highQuality = pendingIt->highQuality || highQuality;
                }
                return;
            }

            ThumbnailRequest request;
            request.key = {imagePath, thumbnailSize, QColorSpace::SRgb};
            request.imagePath = imagePath;
            request.thumbnailSize = thumbnailSize;
            request.colorSpace = QColorSpace::SRgb;
            request.highPriority = highPriority;
            request.highQuality = highQuality;
            m_pendingRequests.insert(requestKey, request);
            if (highPriority) {
                m_highPriorityQueue.enqueue(request);
            } else {
                m_normalPriorityQueue.enqueue(request);
            }
        }
    }

    if (hasCached) {
        QMetaObject::invokeMethod(this, [this, imagePath, cachedThumbnail]() {
            emit thumbnailReady(imagePath, cachedThumbnail);
        }, Qt::QueuedConnection);
        return;
    }

    processQueue();
}

void ImageLoader::processQueue()
{
    while (true) {
        ThumbnailRequest request;

        {
            QMutexLocker locker(&m_cacheMutex);
            if (m_activeLoads >= m_maxConcurrentLoads) {
                return;
            }

            if (!m_highPriorityQueue.isEmpty()) {
                request = m_highPriorityQueue.dequeue();
            } else if (!m_normalPriorityQueue.isEmpty()) {
                request = m_normalPriorityQueue.dequeue();
            } else {
                return;
            }
            const QString requestKey =
                makeRequestKey(request.imagePath, request.thumbnailSize, request.colorSpace);
            auto pendingIt = m_pendingRequests.find(requestKey);
            if (pendingIt == m_pendingRequests.end()) {
                continue;
            }
            request = pendingIt.value();

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
                return std::make_tuple(request.imagePath,
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

            return std::make_tuple(request.imagePath,
                                   request.thumbnailSize,
                                   generated,
                                   modifiedUtc,
                                   request.highQuality,
                                   false);
        });

        auto *watcher = new QFutureWatcher<std::tuple<QString, QSize, QImage, QDateTime, bool, bool>>(this);
        connect(watcher,
                &QFutureWatcher<std::tuple<QString, QSize, QImage, QDateTime, bool, bool>>::finished,
                this,
                [this, watcher]() {
                    auto [imagePath, thumbnailSize, thumbnail, modifiedUtc, highQuality, fromDisk] = watcher->result();
                    watcher->deleteLater();

                    bool cancelledBeforeEmit = false;
                    {
                        QMutexLocker locker(&m_cacheMutex);
                        if (fromDisk && !thumbnail.isNull()) {
                            ++m_metricDiskHits;
                        } else {
                            ++m_metricDecodes;
                        }

                        const QString requestKey =
                            makeRequestKey(imagePath, thumbnailSize, QColorSpace::SRgb);
                        if (m_pendingRequests.contains(requestKey)) {
                            m_pendingRequests.remove(requestKey);
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
        const QString requestKey = makeRequestKey(imagePath, thumbnailSize, QColorSpace::SRgb);
        m_thumbnailCache.insert(requestKey,
                                {thumbnail,
                                 thumbnailSize,
                                 QColorSpace::SRgb,
                                 lastModifiedUtc,
                                 highQuality,
                                 ++m_cacheSequenceCounter});
        trimCache();
    }

    if (!cancelledBeforeEmit) {
        emit thumbnailReady(imagePath, thumbnail);
    }
}

void ImageLoader::requestImage(const QString &imagePath)
{
    auto future = QtConcurrent::run([imagePath]() {
        QImage image(imagePath);
        return image;
    });

    auto *watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this,
            [this, watcher, imagePath]() {
        emit imageReady(imagePath, watcher->result());
        watcher->deleteLater();
    });

    watcher->setFuture(future);
}

QImage ImageLoader::getCachedThumbnail(const QString &imagePath) const
{
    QMutexLocker locker(&m_cacheMutex);
    auto it = m_thumbnailCache.constFind(makeRequestKey(imagePath, QSize(200, 200), QColorSpace::SRgb));
    if (it != m_thumbnailCache.constEnd()) {
        return it->thumbnail;
    }
    return QImage();
}

QImage ImageLoader::getCachedThumbnail(const QString &imagePath, const QSize &thumbnailSize) const
{
    QMutexLocker locker(&m_cacheMutex);
    auto it = m_thumbnailCache.constFind(makeRequestKey(imagePath, thumbnailSize, QColorSpace::SRgb));
    if (it != m_thumbnailCache.constEnd() && it->requestedSize == thumbnailSize) {
        return it->thumbnail;
    }
    return QImage();
}

void ImageLoader::clearCache()
{
    QMutexLocker locker(&m_cacheMutex);
    m_thumbnailCache.clear();
    m_pendingRequests.clear();
    m_highPriorityQueue.clear();
    m_normalPriorityQueue.clear();
}

void ImageLoader::setMaxCacheSize(int maxSize)
{
    m_maxCacheSize = qMax(1, maxSize);
    QMutexLocker locker(&m_cacheMutex);
    trimCache();
}

void ImageLoader::setMaxConcurrentLoads(int maxConcurrentLoads)
{
    {
        QMutexLocker locker(&m_cacheMutex);
        m_maxConcurrentLoads = qBound(1, maxConcurrentLoads, 8);
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

    for (auto it = m_pendingRequests.begin(); it != m_pendingRequests.end();) {
        if (keepPaths.contains(it->imagePath)) {
            ++it;
        } else {
            it = m_pendingRequests.erase(it);
            ++m_metricCancelled;
        }
    }
}

QString ImageLoader::makeRequestKey(const QString &imagePath,
                                    const QSize &thumbnailSize,
                                    QColorSpace::NamedColorSpace colorSpace)
{
    return imagePath + QLatin1Char('|')
           + QString::number(thumbnailSize.width()) + QLatin1Char('x')
           + QString::number(thumbnailSize.height()) + QLatin1Char('|')
           + QString::number(static_cast<int>(colorSpace));
}

void ImageLoader::cancelAllThumbnailRequests()
{
    cancelThumbnailRequestsExcept({});
}

QHash<QString, qint64> ImageLoader::thumbnailMetrics() const
{
    QMutexLocker locker(&m_cacheMutex);
    return {
        {QStringLiteral("requests"), m_metricRequests},
        {QStringLiteral("memoryHits"), m_metricMemoryHits},
        {QStringLiteral("diskHits"), m_metricDiskHits},
        {QStringLiteral("decodes"), m_metricDecodes},
        {QStringLiteral("cancelled"), m_metricCancelled}
    };
}

void ImageLoader::trimCache()
{
    while (m_thumbnailCache.size() > m_maxCacheSize) {
        auto oldestIt = m_thumbnailCache.begin();
        for (auto it = m_thumbnailCache.begin(); it != m_thumbnailCache.end(); ++it) {
            if (it->sequence < oldestIt->sequence) {
                oldestIt = it;
            }
        }
        m_thumbnailCache.erase(oldestIt);
    }
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
    QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty()) {
        base = QDir::tempPath() + QStringLiteral("/ImageCompareCache");
    }

    QDir dir(base);
    dir.mkpath(QStringLiteral("thumbnails"));
    return dir.filePath(QStringLiteral("thumbnails"));
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
