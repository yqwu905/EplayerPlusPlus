#ifndef BROWSEPANEL_H
#define BROWSEPANEL_H

#include <QWidget>
#include <QList>
#include <QTimer>
#include <QSet>

#include "services/VlmAnnotationService.h"

class QHBoxLayout;
class QLabel;
class QVBoxLayout;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QListView;
class QResizeEvent;
class QScrollArea;
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

    /**
     * @brief Enable/disable fuzzy file-name matching for Alt+click selection.
     *
     * The toggle control lives in the main command bar; this lets it drive the
     * matching behaviour without the panel owning the widget.
     */
    void setFuzzyFileNameMatchEnabled(bool enabled);

    /**
     * @brief Live thumbnail geometry, derived from the current browse-column width.
     *
     * Thumbnails scale to fill the column as the splitter is dragged. The delegate
     * (paint + sizeHint) and the list view (hit-testing) read this so they never
     * disagree about where the card/image/mark-buttons are; tests read it to assert
     * that resizing actually rescales. @c decodeExtent is the quantized size images
     * are decoded at (painted scaled to the exact rect).
     */
    struct ThumbMetrics {
        int imageWidth = 154;
        int imageHeight = 96;
        int cardWidth = 166;
        int itemHeight = 142;
        int decodeExtent = 180;
    };

    /** @brief The thumbnail metrics currently in effect. */
    ThumbMetrics thumbnailMetrics() const { return m_metrics; }

    QList<VlmAnnotationService::ColumnSnapshot> currentColumnSnapshots() const;

signals:
    /**
     * @brief Emitted when the image selection changes.
     *
     * Each inner list contains {folderPath, imagePath} pairs for selected images.
     */
    void selectionChanged(const QList<QPair<QString, QString>> &selectedImages);
    void scanStatusChanged(const QString &statusText);

    /**
     * @brief Emitted when the user requests exporting a folder's classification
     * from a thumbnail column's blank-area context menu.
     * @param folderPath Absolute path of the column's folder.
     */
    void exportCategoriesRequested(const QString &folderPath);

public slots:
    /**
     * @brief Navigate to the next image in all columns that have a selection.
     */
    void navigateNext();

    /**
     * @brief Navigate to the previous image in all columns that have a selection.
     */
    void navigatePrevious();

    /**
     * @brief Re-issue thumbnail requests for the visible region of every column.
     *
     * Used after a configuration change (e.g. ICC strip toggled) that invalidated
     * the upstream thumbnail cache: forces visible thumbs to re-decode under the
     * new policy without waiting for a scroll.
     */
    void refreshAllVisibleThumbnails();

private slots:
    void onFolderAdded(const QString &folderPath, int index);
    void onFolderRemoved(const QString &folderPath, int index);
    void onFoldersSwapped(int firstIndex, int secondIndex);
    void onSessionCleared();
    void onFolderReady(int columnIndex);

protected:
    // Splitter drags / window resizes both reach the panel as a resizeEvent, so
    // it is the single hook that keeps thumbnail size in sync with column width.
    void resizeEvent(QResizeEvent *event) override;

private:
    enum class SelectionNavigationMode {
        Independent,
        FileNameMatch
    };

    enum class FilterMatchMode {
        SameIndex,
        FileName
    };

    struct ColumnInfo {
        QWidget *columnWidget = nullptr;
        QVBoxLayout *containerLayout = nullptr;
        QLabel *colorSwatch = nullptr;
        QLabel *progressLabel = nullptr;
        QListView *view = nullptr;
        ImageListModel *model = nullptr;
        int discoveredCount = 0;
        bool scanFinished = false;
        bool thumbnailRequestScheduled = false;
    };

    void setupUi();
    void clearAllColumns();
    void rebuildColumnLayout();
    // Recompute thumbnail size from the current panel width / column count.
    // Display changes relayout the views live (cheap); a decode-bucket change is
    // deferred to m_decodeReloadTimer so a continuous drag never floods the decoder.
    void recomputeThumbnailMetrics();
    void applyMetricsToView(ColumnInfo &col);
    void onDecodeReloadTimeout();
    void updateColumnVisuals(int columnIndex);
    void clearSelection();
    void navigateSelection(int delta);
    bool navigateFileNameMatchedSelection(int delta);
    bool navigateIndependentSelection(int delta);
    void setSelectionNavigationMode(SelectionNavigationMode mode, int anchorColumn);
    void resetSelectionNavigationMode();
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
    // Re-filter the loader's queues to the current visible+prefetch set, but
    // only when that set actually changed since the last call. Re-issuing an
    // identical keep-set is a correctness no-op, so this skips the redundant
    // queue rebuild + generation bump from the many callers that fire on
    // unchanged geometry.
    void cancelStaleThumbnailRequests();
    void updateColumnProgressLabel(int columnIndex);
    void updateGlobalScanStatus();
    void preloadNeighborImagesForSelection();
    void applyCurrentFilters();
    QString currentCategoryFilter() const;
    void updateAllColumnProgressLabels();
    void setCategoryFilterAnchor(int column, FilterMatchMode mode);
    void resetCategoryFilterAnchor();
    bool hasActiveCategoryFilterAnchor() const;
    QSet<QString> matchedImagePathsForColumn(int column,
                                             int anchorColumn,
                                             FilterMatchMode mode) const;
    int findFileNameMatchSourceRow(int column,
                                   const QString &targetFileName) const;

    int columnIndexForModel(const ImageListModel *model) const;

    CompareSession *m_session = nullptr;
    ImageLoader *m_imageLoader = nullptr;
    ImageMarkManager *m_markManager = nullptr;
    QVBoxLayout *m_rootLayout = nullptr;
    QLabel *m_scanStatusLabel = nullptr;
    bool m_fuzzyFileNameMatch = false;
    QLineEdit *m_fileNameFilterEdit = nullptr;
    QComboBox *m_categoryFilterCombo = nullptr;
    QScrollArea *m_columnsScrollArea = nullptr;
    QWidget *m_columnsContainer = nullptr;
    QHBoxLayout *m_columnsLayout = nullptr;
    QList<ColumnInfo> m_columns;
    // Last visible+prefetch keep-set handed to the loader, so an unchanged set
    // skips a redundant cancel/re-filter. See cancelStaleThumbnailRequests.
    QSet<QString> m_lastCancelKeepSet;
    QTimer *m_interleavedLoadTimer = nullptr;
    // Live thumbnail geometry shared (by pointer) with every delegate and list view.
    ThumbMetrics m_metrics;
    // Debounces the higher-resolution re-decode while the splitter is being dragged.
    QTimer *m_decodeReloadTimer = nullptr;
    SelectionNavigationMode m_selectionNavigationMode = SelectionNavigationMode::Independent;
    int m_navigationAnchorColumn = -1;
    int m_categoryFilterAnchorColumn = -1;
    FilterMatchMode m_categoryFilterMatchMode = FilterMatchMode::SameIndex;

    static constexpr int kThumbnailBatchPerTick = 16;
};

#endif // BROWSEPANEL_H
