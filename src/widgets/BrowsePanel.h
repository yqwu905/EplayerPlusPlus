#ifndef BROWSEPANEL_H
#define BROWSEPANEL_H

#include <QWidget>
#include <QList>
#include <QTimer>
#include <QSet>

class QHBoxLayout;
class QLabel;
class QVBoxLayout;
class QCheckBox;
class QListView;
class CompareSession;
class ImageListModel;
class ImageLoader;
class ImageMarkManager;

/**
 * @brief Image browsing panel with multi-column thumbnail layout.
 *
 * Each folder in the CompareSession is displayed as a vertically scrollable
 * column of thumbnails. Supports three selection modes:
 *   - Click: select single image in its column only (other columns unchanged)
 *   - Ctrl+Click: select image + same-index (order) images in other columns
 *   - Alt+Click: select image + filename-matched images in other columns
 */
class BrowsePanel : public QWidget
{
    Q_OBJECT

public:
    explicit BrowsePanel(CompareSession *session, ImageLoader *imageLoader,
                         QWidget *parent = nullptr);
    ~BrowsePanel() override;

    void setImageMarkManager(ImageMarkManager *manager);

signals:
    /**
     * @brief Emitted when the image selection changes.
     *
     * Each inner list contains {folderPath, imagePath} pairs for selected images.
     */
    void selectionChanged(const QList<QPair<QString, QString>> &selectedImages);
    void scanStatusChanged(const QString &statusText);

public slots:
    /**
     * @brief Navigate to the next image in all columns that have a selection.
     */
    void navigateNext();

    /**
     * @brief Navigate to the previous image in all columns that have a selection.
     */
    void navigatePrevious();

private slots:
    void onFolderAdded(const QString &folderPath, int index);
    void onFolderRemoved(const QString &folderPath, int index);
    void onFolderDisplayNameChanged(const QString &folderPath,
                                    int index,
                                    const QString &displayName);
    void onSessionCleared();
    void onFolderReady(int columnIndex);

private:
    struct ColumnInfo {
        QWidget *columnWidget = nullptr;
        QVBoxLayout *containerLayout = nullptr;
        QLabel *headerLabel = nullptr;
        QLabel *progressLabel = nullptr;
        QListView *view = nullptr;
        ImageListModel *model = nullptr;
        int discoveredCount = 0;
        bool scanFinished = false;
        bool thumbnailRequestScheduled = false;
    };

    void setupUi();
    void clearAllColumns();
    void clearSelection();
    void navigateSelection(int delta);
    void clearColumnSelection(int column);
    void onThumbnailActivated(int column, int row, Qt::KeyboardModifiers modifiers);
    void onThumbnailMarkRequested(int column,
                                  int row,
                                  const QString &category,
                                  Qt::KeyboardModifiers modifiers);
    void alignColumnsToAnchor(int anchorColumn,
                              int anchorIndex,
                              const QList<int> &matchedIndices);
    int findFileNameMatchIndex(int column,
                               const QString &targetFileName) const;
    int levenshteinDistance(const QString &a, const QString &b) const;
    void emitSelectionChanged();
    void startInterleavedLoading();
    void stopInterleavedLoading();
    void onInterleavedLoadTick();
    QPair<int, int> visibleRangeForColumn(const ColumnInfo &column) const;
    QPair<int, int> prefetchRangeForColumn(const ColumnInfo &column) const;
    void scheduleThumbnailRequest(int columnIndex, int delayMs);
    void requestThumbnailsForColumn(int columnIndex);
    void requestVisibleThumbnailsForAllColumns();
    QSet<QString> aggregateVisiblePaths() const;
    void updateColumnProgressLabel(int columnIndex);
    void updateGlobalScanStatus();
    void preloadNeighborImagesForSelection();

    int columnIndexForModel(const ImageListModel *model) const;

    CompareSession *m_session = nullptr;
    ImageLoader *m_imageLoader = nullptr;
    ImageMarkManager *m_markManager = nullptr;
    QVBoxLayout *m_rootLayout = nullptr;
    QLabel *m_scanStatusLabel = nullptr;
    QCheckBox *m_fuzzyFileNameCheckBox = nullptr;
    QHBoxLayout *m_columnsLayout = nullptr;
    QList<ColumnInfo> m_columns;
    QTimer *m_interleavedLoadTimer = nullptr;

    static constexpr int kThumbnailBatchPerTick = 16;
};

#endif // BROWSEPANEL_H
