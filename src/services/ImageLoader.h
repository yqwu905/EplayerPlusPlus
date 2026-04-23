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
#include <QColorSpace>

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

    /**
     * @brief Check if a thumbnail is available in cache.
     * @param imagePath Path to the source image.
     * @return Cached thumbnail, or null QImage if not cached.
     */
    QImage getCachedThumbnail(const QString &imagePath) const;
    QImage getCachedThumbnail(const QString &imagePath, const QSize &thumbnailSize) const;

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
    struct RequestKey {
        QString imagePath;
        QSize thumbnailSize;
        QColorSpace::NamedColorSpace colorSpace = QColorSpace::SRgb;

        bool operator==(const RequestKey &other) const
        {
            return imagePath == other.imagePath &&
                   thumbnailSize == other.thumbnailSize &&
                   colorSpace == other.colorSpace;
        }
    };

    struct CacheEntry {
        QImage thumbnail;
        QSize requestedSize;
        QColorSpace::NamedColorSpace colorSpace = QColorSpace::SRgb;
        QDateTime sourceLastModifiedUtc;
        bool highQuality = true;
        qint64 sequence = 0;
    };

    struct ThumbnailRequest {
        RequestKey key;
        QString imagePath;
        QSize thumbnailSize;
        QColorSpace::NamedColorSpace colorSpace = QColorSpace::SRgb;
        bool highPriority = false;
        bool highQuality = true;
    };

    mutable QMutex m_cacheMutex;
    QHash<QString, CacheEntry> m_thumbnailCache;
    QHash<QString, ThumbnailRequest> m_pendingRequests;
    QQueue<ThumbnailRequest> m_highPriorityQueue;
    QQueue<ThumbnailRequest> m_normalPriorityQueue;
    int m_maxCacheSize = 1000;
    int m_maxConcurrentLoads = 4;
    int m_activeLoads = 0;
    qint64 m_cacheSequenceCounter = 0;
    qint64 m_metricRequests = 0;
    qint64 m_metricMemoryHits = 0;
    qint64 m_metricDiskHits = 0;
    qint64 m_metricDecodes = 0;
    qint64 m_metricCancelled = 0;

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

    void enqueueThumbnailRequest(const QString &imagePath,
                                 const QSize &thumbnailSize,
                                 bool highPriority,
                                 bool highQuality);
    void processQueue();
    void finishRequest(const QString &imagePath,
                       const QSize &thumbnailSize,
                       const QImage &thumbnail,
                       const QDateTime &lastModifiedUtc,
                       bool highQuality,
                       bool cancelledBeforeEmit);

    void trimCache();
    static QString makeRequestKey(const QString &imagePath,
                                  const QSize &thumbnailSize,
                                  QColorSpace::NamedColorSpace colorSpace);
};

#endif // IMAGELOADER_H
