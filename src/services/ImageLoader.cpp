#include "ImageLoader.h"
#include "utils/ImageUtils.h"

#include <QtConcurrent>
#include <QMutexLocker>

ImageLoader::ImageLoader(QObject *parent)
    : QObject(parent)
{
}

ImageLoader::~ImageLoader() = default;

void ImageLoader::requestThumbnail(const QString &imagePath, const QSize &thumbnailSize)
{
    // Check cache and pending set under lock
    {
        QMutexLocker locker(&m_cacheMutex);

        // Cache hit — emit immediately
        auto it = m_thumbnailCache.constFind(imagePath);
        if (it != m_thumbnailCache.constEnd() && it->requestedSize == thumbnailSize) {
            QMetaObject::invokeMethod(this, [this, imagePath, thumbnail = it->thumbnail]() {
                emit thumbnailReady(imagePath, thumbnail);
            }, Qt::QueuedConnection);
            return;
        }

        // Already in-flight — skip duplicate submission
        if (m_pendingRequests.contains(imagePath)) {
            return;
        }

        m_pendingRequests.insert(imagePath);
    }

    // Load asynchronously
    auto future = QtConcurrent::run([imagePath, thumbnailSize]() {
        return ImageUtils::generateThumbnail(imagePath, thumbnailSize);
    });

    // Use a watcher to get notified when done
    auto *watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this,
            [this, watcher, imagePath, thumbnailSize]() {
        QImage thumbnail = watcher->result();

        {
            QMutexLocker locker(&m_cacheMutex);
            m_pendingRequests.remove(imagePath);
            if (!thumbnail.isNull()) {
                m_thumbnailCache.insert(imagePath, {thumbnail, thumbnailSize});
                trimCache();
            }
        }

        emit thumbnailReady(imagePath, thumbnail);
        watcher->deleteLater();
    });

    watcher->setFuture(future);
}

void ImageLoader::requestThumbnailBatch(const QStringList &imagePaths, const QSize &thumbnailSize)
{
    // Filter out cached and already-pending paths
    QStringList toLoad;
    {
        QMutexLocker locker(&m_cacheMutex);
        for (const QString &path : imagePaths) {
            // Cache hit — emit immediately via queued connection
            auto it = m_thumbnailCache.constFind(path);
            if (it != m_thumbnailCache.constEnd() && it->requestedSize == thumbnailSize) {
                QMetaObject::invokeMethod(this, [this, path, thumbnail = it->thumbnail]() {
                    emit thumbnailReady(path, thumbnail);
                }, Qt::QueuedConnection);
                continue;
            }

            // Already in-flight — skip duplicate
            if (m_pendingRequests.contains(path)) {
                continue;
            }

            m_pendingRequests.insert(path);
            toLoad.append(path);
        }
    }

    if (toLoad.isEmpty()) {
        return;
    }

    // Split into chunks and submit each as one QtConcurrent task
    for (int start = 0; start < toLoad.size(); start += kBatchChunkSize) {
        QStringList chunk = toLoad.mid(start, kBatchChunkSize);

        auto future = QtConcurrent::run([chunk, thumbnailSize]() {
            QList<QPair<QString, QImage>> results;
            results.reserve(chunk.size());
            for (const QString &path : chunk) {
                results.append({path, ImageUtils::generateThumbnail(path, thumbnailSize)});
            }
            return results;
        });

        auto *watcher = new QFutureWatcher<QList<QPair<QString, QImage>>>(this);
        connect(watcher, &QFutureWatcher<QList<QPair<QString, QImage>>>::finished, this,
                [this, watcher, thumbnailSize]() {
            auto results = watcher->result();

            {
                QMutexLocker locker(&m_cacheMutex);
                for (const auto &[path, thumbnail] : results) {
                    m_pendingRequests.remove(path);
                    if (!thumbnail.isNull()) {
                        m_thumbnailCache.insert(path, {thumbnail, thumbnailSize});
                    }
                }
                trimCache();
            }

            for (const auto &[path, thumbnail] : results) {
                emit thumbnailReady(path, thumbnail);
            }

            watcher->deleteLater();
        });

        watcher->setFuture(future);
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
    auto it = m_thumbnailCache.constFind(imagePath);
    if (it != m_thumbnailCache.constEnd()) {
        return it->thumbnail;
    }
    return QImage();
}

void ImageLoader::clearCache()
{
    QMutexLocker locker(&m_cacheMutex);
    m_thumbnailCache.clear();
    m_pendingRequests.clear();
}

void ImageLoader::setMaxCacheSize(int maxSize)
{
    m_maxCacheSize = qMax(1, maxSize);
    QMutexLocker locker(&m_cacheMutex);
    trimCache();
}

void ImageLoader::trimCache()
{
    // Simple eviction: if cache exceeds max, remove oldest entries
    while (m_thumbnailCache.size() > m_maxCacheSize) {
        m_thumbnailCache.erase(m_thumbnailCache.begin());
    }
}
