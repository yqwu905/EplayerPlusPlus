#ifndef BROWSEPANEL_H
#define BROWSEPANEL_H

#include <QWidget>
#include <QList>
#include <QTimer>
#include <QSet>

class QHBoxLayout;
class QScrollArea;
class QLabel;
class QVBoxLayout;
class QCheckBox;
class CompareSession;
class ImageListModel;
class ImageLoader;
class ThumbnailWidget;

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

signals:
    /**
     * @brief Emitted when the image selection changes.
     *
     * Each inner list contains {folderPath, imagePath} pairs for selected images.
     */
    void selectionChanged(const QList<QPair<QString, QString>> &selectedImages);

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
    void onSessionCleared();
    void onThumbnailClicked(const QString &filePath, Qt::KeyboardModifiers modifiers);
    void onFolderReady(int columnIndex);
    void onViewportUpdateTick();

private:
    struct ColumnInfo {
        QScrollArea *scrollArea = nullptr;
        QWidget *container = nullptr;
        QVBoxLayout *containerLayout = nullptr;
        QLabel *loadingLabel = nullptr;
        ImageListModel *model = nullptr;
        QList<ThumbnailWidget *> thumbnailWidgets;
        int builtCount = 0;
    };

    void setupUi();
    void rebuildColumn(int columnIndex);
    void buildThumbnailsBatch(int columnIndex);
    void clearAllColumns();
    void clearSelection();
    void navigateSelection(int delta);
    void clearColumnSelection(int column);
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
    void requestVisibleThumbnailsForAllColumns();
    QSet<QString> aggregateVisiblePaths() const;
    void scheduleViewportUpdate();

    bool findThumbnailPosition(const ThumbnailWidget *thumbnail,
                               int &column,
                               int &indexInColumn) const;

    CompareSession *m_session = nullptr;
    ImageLoader *m_imageLoader = nullptr;
    QVBoxLayout *m_rootLayout = nullptr;
    QCheckBox *m_fuzzyFileNameCheckBox = nullptr;
    QHBoxLayout *m_columnsLayout = nullptr;
    QList<ColumnInfo> m_columns;
    QTimer *m_interleavedLoadTimer = nullptr;
    QTimer *m_viewportUpdateTimer = nullptr;

    static constexpr int kBatchSize = 50;
    static constexpr int kViewportThrottleMs = 24;
    static constexpr int kPrefetchScreens = 3;
};

#endif // BROWSEPANEL_H
