#ifndef IMAGELISTMODEL_H
#define IMAGELISTMODEL_H

#include <QAbstractListModel>
#include <QStringList>
#include <QImage>
#include <QHash>
#include <QFutureWatcher>

class ImageLoader;

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
        IsSelectedRole
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

    // ---- Thumbnail loading ----

    /**
     * @brief Set the shared ImageLoader instance.
     */
    void setImageLoader(ImageLoader *loader);

    /**
     * @brief Request thumbnail loading for visible items.
     * @param firstVisible First visible index.
     * @param lastVisible Last visible index.
     */
    void loadThumbnailsForRange(int firstVisible, int lastVisible);

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

private slots:
    void onThumbnailReady(const QString &imagePath, const QImage &thumbnail);

private:
    void onScanFinished();
    void cancelPendingScan();

    QString m_folderPath;
    QStringList m_imagePaths;
    QHash<QString, int> m_pathToIndex;   // path -> index for O(1) lookup
    QSet<int> m_selectedIndices;
    QHash<QString, QImage> m_thumbnails;
    ImageLoader *m_imageLoader = nullptr;
    QFutureWatcher<QStringList> *m_scanWatcher = nullptr;
    bool m_loading = false;
    int m_nextLoadIndex = 0;
};

#endif // IMAGELISTMODEL_H
