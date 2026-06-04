#ifndef IMAGELISTMODEL_H
#define IMAGELISTMODEL_H

#include <QAbstractListModel>
#include <QStringList>
#include <QImage>
#include <QHash>
#include <QDateTime>
#include <QSize>
#include <QVector>
#include <QFuture>
#include <list>
#include <memory>

#include "utils/FileUtils.h"

class ImageLoader;
class ImageMarkManager;

/**
 * @brief List model for images within a single folder.
 *
 * Provides image path, filename, and thumbnail data for display
 * in the browse panel. Uses ImageLoader for async thumbnail loading.
 */
class ImageListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        FilePathRole = Qt::UserRole + 1,
        FileNameRole,
        ThumbnailRole,
        IsSelectedRole,
        MarkRole
    };

    explicit ImageListModel(QObject *parent = nullptr);
    ~ImageListModel() override;

    // ---- QAbstractListModel interface ----
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    // ---- Folder and image management ----

    /**
     * @brief Set the folder to display images from.
     * @param folderPath Absolute path to the folder.
     */
    void setFolder(const QString &folderPath);

    /**
     * @brief Get the current folder path.
     */
    QString folderPath() const;

    /**
     * @brief Get the folder display name.
     */
    QString folderName() const;

    /**
     * @brief Refresh the image list by re-scanning the folder.
     */
    void refresh();

    /**
     * @brief Check if the model is currently loading (async scan in progress).
     */
    bool isLoading() const;

    /**
     * @brief Get the image file path at a given index.
     */
    QString imagePathAt(int index) const;

    /**
     * @brief Get the filename (without path) at a given index.
     */
    QString fileNameAt(int index) const;

    /**
     * @brief Get the total number of images.
     */
    int imageCount() const;

    /**
     * @brief Get the total number of images before active filters are applied.
     */
    int unfilteredImageCount() const;

    /**
     * @brief Apply a case-insensitive filename substring filter.
     */
    void setFileNameFilter(const QString &filterText);
    QString fileNameFilter() const;

    /**
     * @brief Apply an image mark/category filter. Empty means all categories.
     */
    void setCategoryFilter(const QString &category);
    QString categoryFilter() const;
    bool hasActiveFilters() const;

    /**
     * @brief Find the index of an image by its filename.
     * @param fileName Filename (without path) to search for.
     * @return Index, or -1 if not found.
     */
    int indexOfFileName(const QString &fileName) const;

    // ---- Selection management ----

    /**
     * @brief Set selection state for an image.
     */
    void setSelected(int index, bool selected);

    /**
     * @brief Clear all selections.
     */
    void clearSelection();

    /**
     * @brief Get list of selected image indices.
     */
    QList<int> selectedIndices() const;

    /**
     * @brief Check if an image at index is selected.
     */
    bool isSelected(int index) const;

    // ---- Image mark management ----

    /**
     * @brief Set the shared image mark manager.
     */
    void setImageMarkManager(ImageMarkManager *manager);

    /**
     * @brief Get the A/B/C/D mark for an image, or an empty string if unmarked.
     */
    QString markAt(int index) const;

    /**
     * @brief Set the A/B/C/D mark for an image.
     */
    bool setMarkAt(int index, const QString &category);

    // ---- Thumbnail loading ----

    /**
     * @brief Set the shared ImageLoader instance.
     */
    void setImageLoader(ImageLoader *loader);

    /**
     * @brief Set the size thumbnails are decoded at.
     *
     * Driven by BrowsePanel as the user resizes the browse column (zoom). The
     * value is the (square) decode "bucket"; the delegate paints the result
     * scaled to the exact on-screen rect. Growing the size lets visible thumbnails
     * be re-requested at a sharper resolution; shrinking keeps the larger cached
     * image and downscales it on paint.
     */
    void setThumbnailSize(const QSize &size);
    QSize thumbnailSize() const;

    /**
     * @brief Request thumbnail loading for a row range.
     * @param firstVisible First index in the range.
     * @param lastVisible Last index in the range.
     * @param prefetchPriority If true, enqueue at the loader's prefetch priority
     *        (for off-screen margins) instead of visible priority, so prefetch
     *        work cannot starve on-screen rows in the decode pool.
     */
    void loadThumbnailsForRange(int firstVisible, int lastVisible,
                                bool prefetchPriority = false);

    /**
     * @brief Load the next batch of thumbnails (for interleaved loading).
     * @param batchSize Number of thumbnails to request in this batch.
     * @return true if there are more thumbnails to load.
     */
    bool loadNextThumbnailBatch(int batchSize = 6);

    /**
     * @brief Check if there are more thumbnails to load.
     */
    bool hasMoreToLoad() const;

signals:
    /**
     * @brief Emitted when the folder scan is complete and the image list is ready.
     */
    void folderReady();

    /**
     * @brief Emitted when selected images change.
     */
    void selectionChanged();
    void scanProgressChanged(int discoveredCount, bool finished);

private slots:
    void onThumbnailReady(const QString &imagePath, const QImage &thumbnail);
    void onMarkChanged(const QString &folderPath,
                       const QString &imagePath,
                       const QString &category);

private:
    void startScan(const QString &path);
    void appendScanBatch(const QVector<FileUtils::ScannedImage> &batch, int generation);
    void finalizeScan(int generation);
    // Reorder the source arrays into globally sorted (by path) order. The scan
    // delivers images in discovery order so they paint immediately; this runs once
    // when the scan completes to put the list into its final, comparison-stable
    // order. No-op when the list is already sorted.
    void sortSourcesByPath();
    void cancelPendingScan();
    int sourceIndexForRow(int row) const;
    int rowForSourceIndex(int sourceIndex) const;
    bool sourceImageMatchesFilters(int sourceIndex) const;
    void rebuildFilteredRows();
    void applyFilters();
    void updateFilteredRowForSourceIndex(int sourceIndex);
    QString markAtSourceIndex(int sourceIndex) const;

    // ---- Thumbnail cache (bounded, access-ordered) ----
    // m_thumbnails grows linearly with the number of distinct images viewed, and
    // each entry gets larger as the user zooms in, so it is capped with an LRU.
    // The visible + prefetch set is "touched" every load cycle, so it is never
    // the eviction victim; an evicted-then-revisited path is simply re-requested.
    void touchThumbnail(const QString &path);
    void storeThumbnail(const QString &path, const QImage &thumbnail);
    void trimThumbnailCache();
    void clearThumbnailCache();

    QString m_folderPath;
    QString m_normalizedFolderPath;
    QStringList m_imagePaths;
    QHash<QString, int> m_pathToIndex;   // path -> index for O(1) lookup
    QStringList m_markKeys;
    QHash<QString, int> m_markKeyToIndex;
    QStringList m_fileNames;
    QHash<QString, int> m_fileNameToIndex;
    QList<int> m_filteredSourceRows;
    QString m_fileNameFilter;
    QString m_categoryFilter;
    QSet<int> m_selectedIndices;
    QHash<QString, QImage> m_thumbnails;
    // LRU bookkeeping for m_thumbnails: MRU at the front, LRU at the back.
    // m_thumbnailLruPos maps a path to its node for O(1) splice-to-front.
    std::list<QString> m_thumbnailLru;
    QHash<QString, std::list<QString>::iterator> m_thumbnailLruPos;
    // Size thumbnails are currently decoded at (the zoom "bucket"). Square.
    QSize m_thumbnailSize = QSize(180, 180);
    // path -> largest bucket we have already issued a request at, so a zoom-in
    // upgrades each visible thumbnail at most once per bucket (and tiny source
    // images are not re-requested forever trying to reach an unreachable size).
    QHash<QString, int> m_upgradedExtent;
    // path -> source last-modified time captured during the folder scan, passed
    // to ImageLoader so the decode worker can skip a redundant stat() per thumbnail.
    QHash<QString, QDateTime> m_sourceModifiedUtc;
    ImageLoader *m_imageLoader = nullptr;
    ImageMarkManager *m_markManager = nullptr;
    std::shared_ptr<FileUtils::ScanCancelToken> m_scanCancelToken;
    // Handle to the background scan started in startScan(). Retained so teardown
    // (cancelPendingScan) can block until the worker has actually returned; a
    // discarded future would let the worker outlive this model and dereference it.
    QFuture<void> m_scanFuture;
    bool m_loading = false;
    int m_nextLoadIndex = 0;
    int m_scanGeneration = 0;
    int m_initialPrefetchRemaining = 0;
};

#endif // IMAGELISTMODEL_H
