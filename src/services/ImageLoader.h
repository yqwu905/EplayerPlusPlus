#ifndef IMAGELOADER_H
#define IMAGELOADER_H

#include <QObject>
#include <QImage>
#include <QHash>
#include <QSet>
#include <QMutex>
#include <QSize>
#include <QDateTime>
#include <QQueue>

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
    void requestThumbnailBatchVisibleFirst(const QStringList &imagePaths, const QSize &thumbnailSize = QSize(200, 200));

    /**
     * @brief Request asynchronous loading of a full-size image.
     * @param imagePath Path to the source image.
     *
     * When loading is complete, the imageReady signal is emitted.
     */
    void requestImage(const QString &imagePath);
    void requestImageBatch(const QStringList &imagePaths);

    /**
     * @brief Check if a thumbnail is available in cache.
     * @param imagePath Path to the source image.
     * @return Cached thumbnail, or null QImage if not cached.
     */
    QImage getCachedThumbnail(const QString &imagePath) const;
    QImage getCachedThumbnail(const QString &imagePath, const QSize &thumbnailSize) const;
    QImage getCachedImage(const QString &imagePath) const;

    /**
     * @brief Clear the thumbnail cache.
     */
    void clearCache();

    /**
     * @brief Set the maximum number of cached thumbnails.
     * @param maxSize Maximum cache size. Default is 1000.
     */
    void setMaxCacheSize(int maxSize);
    void setMaxConcurrentLoads(int maxConcurrentLoads);
    void cancelThumbnailRequestsExcept(const QSet<QString> &keepPaths);
    void cancelAllThumbnailRequests();
    void cancelImageRequestsExcept(const QSet<QString> &keepPaths);
    QHash<QString, qint64> thumbnailMetrics() const;

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
        QString imagePath;
        QImage thumbnail;
        QSize requestedSize;
        QDateTime sourceLastModifiedUtc;
        bool highQuality = true;
        qint64 sequence = 0;
        qint64 byteSize = 0;
    };

    struct ImageCacheEntry {
        QImage image;
        qint64 sequence = 0;
        qint64 byteSize = 0;
    };

    struct ThumbnailRequest {
        QString key;
        QString imagePath;
        QSize thumbnailSize;
        int priority = 0;
        bool highQuality = true;
    };

    mutable QMutex m_cacheMutex;
    QHash<QString, CacheEntry> m_thumbnailCache;
    QHash<QString, ImageCacheEntry> m_imageCache;
    QHash<QString, ThumbnailRequest> m_pendingRequests;
    QSet<QString> m_pendingImageRequests;
    QQueue<ThumbnailRequest> m_highPriorityQueue;
    QQueue<ThumbnailRequest> m_normalPriorityQueue;
    QQueue<ThumbnailRequest> m_backgroundPriorityQueue;
    QQueue<QString> m_imageQueue;
    int m_maxCacheSize = 1000;
    qint64 m_maxThumbnailCacheBytes = 256LL * 1024LL * 1024LL;
    qint64 m_currentThumbnailCacheBytes = 0;
    int m_maxImageCacheSize = 64;
    qint64 m_maxImageCacheBytes = 512LL * 1024LL * 1024LL;
    qint64 m_currentImageCacheBytes = 0;
    int m_maxConcurrentLoads = 4;
    int m_activeLoads = 0;
    int m_maxConcurrentImageLoads = 2;
    int m_activeImageLoads = 0;
    qint64 m_cacheSequenceCounter = 0;
    qint64 m_imageCacheSequenceCounter = 0;
    qint64 m_metricRequests = 0;
    qint64 m_metricMemoryHits = 0;
    qint64 m_metricDiskHits = 0;
    qint64 m_metricDecodes = 0;
    qint64 m_metricCancelled = 0;

    static QString memoryCacheKey(const QString &imagePath,
                                  const QSize &thumbnailSize,
                                  bool highQuality);
    static QString makeCacheKey(const QString &imagePath,
                                const QSize &thumbnailSize,
                                const QDateTime &lastModifiedUtc,
                                bool highQuality);
    static QString cacheRootDir();
    static QString cachePathForKey(const QString &cacheKey);
    static QDateTime sourceLastModifiedUtc(const QString &imagePath);
    static QImage tryLoadDiskCachedThumbnail(const QString &imagePath,
                                             const QSize &thumbnailSize,
                                             const QDateTime &lastModifiedUtc,
                                             bool highQuality);
    static void persistDiskThumbnail(const QString &imagePath,
                                     const QSize &thumbnailSize,
                                     const QDateTime &lastModifiedUtc,
                                     const QImage &thumbnail,
                                     bool highQuality);
    static qint64 imageByteSize(const QImage &image);

    void enqueueThumbnailRequest(const QString &imagePath,
                                 const QSize &thumbnailSize,
                                 int priority,
                                 bool highQuality);
    void enqueueRequestUnlocked(const ThumbnailRequest &request);
    void processQueue();
    void finishRequest(const QString &imagePath,
                       const QSize &thumbnailSize,
                       const QImage &thumbnail,
                       const QDateTime &lastModifiedUtc,
                       bool highQuality,
                       bool cancelledBeforeEmit);
    void processImageQueue();
    void finishImageRequest(const QString &imagePath, const QImage &image, bool cancelledBeforeEmit);

    void trimCache();
    void trimImageCache();
};

#endif // IMAGELOADER_H
