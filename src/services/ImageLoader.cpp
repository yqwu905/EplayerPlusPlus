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
    // Check cache first
    {
        QMutexLocker locker(&m_cacheMutex);
        auto it = m_thumbnailCache.constFind(imagePath);
        if (it != m_thumbnailCache.constEnd() && it->requestedSize == thumbnailSize) {
            // Cache hit — emit immediately via queued connection to avoid re-entrancy
            QMetaObject::invokeMethod(this, [this, imagePath, thumbnail = it->thumbnail]() {
                emit thumbnailReady(imagePath, thumbnail);
            }, Qt::QueuedConnection);
            return;
        }
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

        if (!thumbnail.isNull()) {
            // Store in cache
            QMutexLocker locker(&m_cacheMutex);
            m_thumbnailCache.insert(imagePath, {thumbnail, thumbnailSize});
            trimCache();
        }

        emit thumbnailReady(imagePath, thumbnail);
        watcher->deleteLater();
    });

    watcher->setFuture(future);
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
