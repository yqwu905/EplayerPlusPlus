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
     * Enqueues the paths through the shared deduplicating priority scheduler.
     * Each completed thumbnail emits thumbnailReady individually.
     */
    void requestThumbnailBatch(const QStringList &imagePaths, const QSize &thumbnailSize = QSize(200, 200),
                               const QHash<QString, QDateTime> &sourceModifiedUtc = {});
    void requestThumbnailBatchVisibleFirst(const QStringList &imagePaths, const QSize &thumbnailSize = QSize(200, 200),
                                           const QHash<QString, QDateTime> &sourceModifiedUtc = {});
    // Like requestThumbnailBatchVisibleFirst but enqueues at prefetch priority
    // (below visible, above background) with the same fast decode quality, so
    // off-screen prefetch margins can't starve on-screen rows in the decode
    // pool. A prefetched item later re-requested at visible priority is bumped
    // up by the pending-priority logic in enqueueThumbnailRequest.
    void requestThumbnailBatchPrefetch(const QStringList &imagePaths, const QSize &thumbnailSize = QSize(200, 200),
                                       const QHash<QString, QDateTime> &sourceModifiedUtc = {});

    /**
     * @brief Request asynchronous loading of a full-size image.
     * @param imagePath Path to the source image.
     *
     * When loading is complete, the imageReady signal is emitted.
     */
    void requestImage(const QString &imagePath);
    void requestImageBatch(const QStringList &imagePaths);
    // Low-priority, silent full-image prefetch. Results warm the LRU but do not
    // emit imageReady unless a visible request promotes the same in-flight work.
    void prefetchImages(const QStringList &imagePaths);

    /**
     * @brief Check if a thumbnail is available in cache.
     * @param imagePath Path to the source image.
     * @return Cached thumbnail, or null QImage if not cached.
     */
    QImage getCachedThumbnail(const QString &imagePath);
    QImage getCachedThumbnail(const QString &imagePath, const QSize &thumbnailSize);
    QImage getCachedImage(const QString &imagePath);

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
     * new policy. Queued work is dropped and in-flight old-policy results are
     * rejected; callers should then re-request their visible/current paths.
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
    void thumbnailReadyDetailed(const QString &imagePath,
                                const QImage &thumbnail,
                                const QSize &requestedSize,
                                bool highQuality);

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
        quint64 token = 0;
    };

    struct ImageRequest {
        QString imagePath;
        int priority = 0;
        bool notify = false;
        quint64 token = 0;
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

    struct DiskWriteState {
        QMutex mutex;
        QSet<QString> pendingKeys;
        int pendingCount = 0;
        qint64 pendingBytes = 0;
        qint64 droppedCount = 0;
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
    QHash<QString, quint64> m_activeThumbnailRequests;
    QHash<QString, ImageRequest> m_pendingImageRequests;
    QHash<QString, quint64> m_activeImageRequests;
    QQueue<ThumbnailRequest> m_highPriorityQueue;
    QQueue<ThumbnailRequest> m_normalPriorityQueue;
    QQueue<ThumbnailRequest> m_backgroundPriorityQueue;
    QQueue<ImageRequest> m_visibleImageQueue;
    QQueue<ImageRequest> m_prefetchImageQueue;
    // Shared cancellation state. Workers capture these by shared_ptr so
    // they remain valid even if the ImageLoader is destroyed mid-decode.
    std::shared_ptr<CancellationState> m_thumbnailCancel;
    std::shared_ptr<CancellationState> m_imageCancel;
    std::shared_ptr<DiskWriteState> m_diskWriteState =
        std::make_shared<DiskWriteState>();
    // Dedicated pool for decode/load workers. The global pool caps at
    // ~idealThreadCount (CPU cores), which throttles concurrent reads from a
    // network share; a dedicated pool lets us run more in-flight loads to
    // overlap I/O latency. Drained explicitly in the destructor.
    // Heap ownership lets shutdown abandon a worker pool after a bounded wait if
    // the OS is stuck inside an uninterruptible network-file read. Workers never
    // capture ImageLoader itself, so intentionally leaking that rare pool is safe
    // and preferable to hanging application exit indefinitely.
    std::unique_ptr<QThreadPool> m_decodePool;
    std::unique_ptr<QThreadPool> m_imageDecodePool;
    std::unique_ptr<QThreadPool> m_diskCachePool;
    int m_maxCacheSize = 1000;
    qint64 m_maxThumbnailCacheBytes = 256LL * 1024LL * 1024LL;
    qint64 m_currentThumbnailCacheBytes = 0;
    int m_maxImageCacheSize = 64;
    qint64 m_maxImageCacheBytes = 512LL * 1024LL * 1024LL;
    qint64 m_currentImageCacheBytes = 0;
    int m_maxConcurrentLoads = 8;
    int m_activeLoads = 0;
    int m_maxConcurrentImageLoads = 3;
    int m_maxConcurrentImagePrefetchLoads = 1;
    int m_activeImageLoads = 0;
    int m_activeImagePrefetchLoads = 0;
    quint64 m_nextRequestToken = 1;
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
    static void pruneDiskCache(qint64 byteBudget = 512LL * 1024LL * 1024LL,
                               bool respectMaintenanceStamp = true);
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
    void enqueueImageRequest(const QString &imagePath, int priority, bool notify);
    void enqueueImageRequestUnlocked(const ImageRequest &request);
    void processQueue();
    void finishRequest(const QString &imagePath,
                       const QSize &thumbnailSize,
                       const QImage &thumbnail,
                       const QDateTime &lastModifiedUtc,
                       bool highQuality,
                       bool ignoreColorProfile,
                       bool cancelledBeforeEmit);
    void processImageQueue();
    void finishImageRequest(const QString &imagePath,
                            const QImage &image,
                            bool accepted,
                            bool notify);
    void scheduleDiskThumbnailWrite(const QString &imagePath,
                                    const QSize &thumbnailSize,
                                    const QDateTime &lastModifiedUtc,
                                    const QImage &thumbnail,
                                    bool highQuality,
                                    bool ignoreColorProfile);

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
