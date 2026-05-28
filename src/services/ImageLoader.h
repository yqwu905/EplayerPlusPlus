#ifndef IMAGELOADER_H
#define IMAGELOADER_H

#include <QObject>
#include <QImage>
#include <QHash>
#include <QList>
#include <QSet>
#include <QMutex>
#include <QSize>
#include <QDateTime>
#include <QQueue>
#include <QThreadPool>

#include <atomic>
#include <list>
#include <memory>

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
    void requestThumbnailBatch(const QStringList &imagePaths, const QSize &thumbnailSize = QSize(200, 200),
                               const QHash<QString, QDateTime> &sourceModifiedUtc = {});
    void requestThumbnailBatchVisibleFirst(const QStringList &imagePaths, const QSize &thumbnailSize = QSize(200, 200),
                                           const QHash<QString, QDateTime> &sourceModifiedUtc = {});

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

    /**
     * @brief Whether decoded images have their embedded ICC profile stripped.
     */
    bool ignoreColorProfile() const;

    /**
     * @brief Configure whether decoded images should have their ICC profile stripped.
     *
     * When the flag changes, the in-memory thumbnail and full-image caches are
     * cleared so subsequent requests see freshly-decoded images that reflect the
     * new policy. In-flight workers continue using the value they snapshotted at
     * enqueue time, but their results are correctly keyed in the (possibly
     * cleared) cache by that snapshotted flag.
     */
    void setIgnoreColorProfile(bool enabled);
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
        QString key;
        QString imagePath;
        QImage thumbnail;
        QSize requestedSize;
        QDateTime sourceLastModifiedUtc;
        bool highQuality = true;
        bool ignoreColorProfile = false;
        qint64 byteSize = 0;
    };

    struct ImageCacheEntry {
        QString imagePath;
        QImage image;
        qint64 byteSize = 0;
    };

    struct ThumbnailRequest {
        QString key;
        QString imagePath;
        QSize thumbnailSize;
        int priority = 0;
        bool highQuality = true;
        // Snapshotted at enqueue time so a runtime toggle does not race the
        // decoder vs. the cache-key write: both use this captured value.
        bool ignoreColorProfile = false;
        // Source mtime captured during the folder scan. If invalid, the decode
        // worker falls back to stat()ing the file itself.
        QDateTime sourceLastModifiedUtc;
    };

    using ThumbnailList = std::list<CacheEntry>;
    using ThumbnailIter = ThumbnailList::iterator;
    using ImageList = std::list<ImageCacheEntry>;
    using ImageIter = ImageList::iterator;

    // Cancellation state shared between the loader and any in-flight
    // QtConcurrent worker. Held by shared_ptr so workers can safely
    // outlive the ImageLoader. The mutex protects the keep-paths set.
    struct CancellationState {
        std::atomic<int> generation{0};
        QMutex keepMutex;
        QSet<QString> keepPaths;
    };

    mutable QMutex m_cacheMutex;
    // Intrusive LRU: list is ordered MRU (front) -> LRU (back). O(1) splice
    // on access, O(1) pop_back on eviction.
    ThumbnailList m_thumbnailLru;
    QHash<QString, ThumbnailIter> m_thumbnailIndex;
    // Secondary index path -> list of cache keys, so the no-size
    // getCachedThumbnail() lookup avoids a linear scan of every entry.
    QHash<QString, QList<QString>> m_thumbnailPathIndex;
    ImageList m_imageLru;
    QHash<QString, ImageIter> m_imageIndex;
    QHash<QString, ThumbnailRequest> m_pendingRequests;
    QSet<QString> m_pendingImageRequests;
    QQueue<ThumbnailRequest> m_highPriorityQueue;
    QQueue<ThumbnailRequest> m_normalPriorityQueue;
    QQueue<ThumbnailRequest> m_backgroundPriorityQueue;
    QQueue<QString> m_imageQueue;
    // Shared cancellation state. Workers capture these by shared_ptr so
    // they remain valid even if the ImageLoader is destroyed mid-decode.
    std::shared_ptr<CancellationState> m_thumbnailCancel;
    std::shared_ptr<CancellationState> m_imageCancel;
    // Dedicated pool for decode/load workers. The global pool caps at
    // ~idealThreadCount (CPU cores), which throttles concurrent reads from a
    // network share; a dedicated pool lets us run more in-flight loads to
    // overlap I/O latency. Drained explicitly in the destructor.
    QThreadPool m_decodePool;
    int m_maxCacheSize = 1000;
    qint64 m_maxThumbnailCacheBytes = 256LL * 1024LL * 1024LL;
    qint64 m_currentThumbnailCacheBytes = 0;
    int m_maxImageCacheSize = 64;
    qint64 m_maxImageCacheBytes = 512LL * 1024LL * 1024LL;
    qint64 m_currentImageCacheBytes = 0;
    int m_maxConcurrentLoads = 8;
    int m_activeLoads = 0;
    int m_maxConcurrentImageLoads = 2;
    int m_activeImageLoads = 0;
    // Strip ICC profile from decoded images. Atomic so the enqueue path can
    // snapshot it lock-free without taking m_cacheMutex.
    std::atomic<bool> m_ignoreColorProfile{true};
    qint64 m_metricRequests = 0;
    qint64 m_metricMemoryHits = 0;
    qint64 m_metricDiskHits = 0;
    qint64 m_metricDecodes = 0;
    qint64 m_metricCancelled = 0;

    static QString memoryCacheKey(const QString &imagePath,
                                  const QSize &thumbnailSize,
                                  bool highQuality,
                                  bool ignoreColorProfile);
    static QString makeCacheKey(const QString &imagePath,
                                const QSize &thumbnailSize,
                                const QDateTime &lastModifiedUtc,
                                bool highQuality,
                                bool ignoreColorProfile);
    static QString cacheRootDir();
    static QString cachePathForKey(const QString &cacheKey);
    static QDateTime sourceLastModifiedUtc(const QString &imagePath);
    static QImage tryLoadDiskCachedThumbnail(const QString &imagePath,
                                             const QSize &thumbnailSize,
                                             const QDateTime &lastModifiedUtc,
                                             bool highQuality,
                                             bool ignoreColorProfile);
    static void persistDiskThumbnail(const QString &imagePath,
                                     const QSize &thumbnailSize,
                                     const QDateTime &lastModifiedUtc,
                                     const QImage &thumbnail,
                                     bool highQuality,
                                     bool ignoreColorProfile);
    static qint64 imageByteSize(const QImage &image);

    void enqueueThumbnailRequest(const QString &imagePath,
                                 const QSize &thumbnailSize,
                                 int priority,
                                 bool highQuality,
                                 const QDateTime &sourceModifiedUtc = {});
    void enqueueRequestUnlocked(const ThumbnailRequest &request);
    void processQueue();
    void finishRequest(const QString &imagePath,
                       const QSize &thumbnailSize,
                       const QImage &thumbnail,
                       const QDateTime &lastModifiedUtc,
                       bool highQuality,
                       bool ignoreColorProfile,
                       bool cancelledBeforeEmit);
    void processImageQueue();
    void finishImageRequest(const QString &imagePath, const QImage &image, bool cancelledBeforeEmit);

    void touchThumbnailEntryUnlocked(ThumbnailIter it);
    void insertThumbnailEntryUnlocked(CacheEntry &&entry);
    void eraseThumbnailEntryUnlocked(ThumbnailIter it);
    void touchImageEntryUnlocked(ImageIter it);
    void insertImageEntryUnlocked(ImageCacheEntry &&entry);
    void eraseImageEntryUnlocked(ImageIter it);

    void trimCache();
    void trimImageCache();
};

#endif // IMAGELOADER_H
