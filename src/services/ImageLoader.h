#ifndef IMAGELOADER_H
#define IMAGELOADER_H

#include <QObject>
#include <QImage>
#include <QHash>
#include <QSet>
#include <QMutex>
#include <QSize>

/**
 * @brief Asynchronous image and thumbnail loading service with caching.
 *
 * Uses QtConcurrent to load images in background threads. Maintains an
 * in-memory cache of generated thumbnails for performance.
 */
class ImageLoader : public QObject
{
    Q_OBJECT

public:
    explicit ImageLoader(QObject *parent = nullptr);
    ~ImageLoader() override;

    /**
     * @brief Request asynchronous loading of a thumbnail.
     * @param imagePath Path to the source image.
     * @param thumbnailSize Desired thumbnail size.
     *
     * When loading is complete, the thumbnailReady signal is emitted.
     * If the thumbnail is already cached, the signal is emitted immediately.
     */
    void requestThumbnail(const QString &imagePath, const QSize &thumbnailSize = QSize(200, 200));

    /**
     * @brief Request asynchronous loading of a batch of thumbnails.
     * @param imagePaths List of paths to source images.
     * @param thumbnailSize Desired thumbnail size.
     *
     * More efficient than calling requestThumbnail() in a loop because
     * it groups work into fewer QtConcurrent tasks and QFutureWatcher objects.
     * Each completed thumbnail still emits thumbnailReady individually.
     */
    void requestThumbnailBatch(const QStringList &imagePaths, const QSize &thumbnailSize = QSize(200, 200));

    /**
     * @brief Request asynchronous loading of a full-size image.
     * @param imagePath Path to the source image.
     *
     * When loading is complete, the imageReady signal is emitted.
     */
    void requestImage(const QString &imagePath);

    /**
     * @brief Check if a thumbnail is available in cache.
     * @param imagePath Path to the source image.
     * @return Cached thumbnail, or null QImage if not cached.
     */
    QImage getCachedThumbnail(const QString &imagePath) const;

    /**
     * @brief Clear the thumbnail cache.
     */
    void clearCache();

    /**
     * @brief Set the maximum number of cached thumbnails.
     * @param maxSize Maximum cache size. Default is 1000.
     */
    void setMaxCacheSize(int maxSize);

signals:
    /**
     * @brief Emitted when a thumbnail has been loaded.
     * @param imagePath Path to the source image.
     * @param thumbnail The generated thumbnail.
     */
    void thumbnailReady(const QString &imagePath, const QImage &thumbnail);

    /**
     * @brief Emitted when a full-size image has been loaded.
     * @param imagePath Path to the source image.
     * @param image The loaded image.
     */
    void imageReady(const QString &imagePath, const QImage &image);

private:
    struct CacheEntry {
        QImage thumbnail;
        QSize requestedSize;
    };

    mutable QMutex m_cacheMutex;
    QHash<QString, CacheEntry> m_thumbnailCache;
    QSet<QString> m_pendingRequests;
    int m_maxCacheSize = 1000;

    static constexpr int kBatchChunkSize = 16;

    void trimCache();
};

#endif // IMAGELOADER_H
