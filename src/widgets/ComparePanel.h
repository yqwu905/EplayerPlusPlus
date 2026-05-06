#ifndef COMPAREPANEL_H
#define COMPAREPANEL_H

#include <QWidget>
#include <QList>
#include <QPair>
#include <QImage>

class QGridLayout;
class QSlider;
class QLabel;
class QToolBar;
class QAction;
class QHBoxLayout;
class QPushButton;
class QCheckBox;
class SettingsManager;
class CompareSession;
class ZoomableImageWidget;
class ImageLoader;
class ImageMarkManager;

/**
 * @brief Image comparison panel displaying selected images in a grid.
 *
 * Maintains one cell per folder in the CompareSession. Each cell provides
 * compare buttons to trigger operations against other visible images.
 * Two compare modes:
 *   - Swap mode (default): press-hold compare button to preview source on target,
 *     release to restore.
 *   - Tolerance mode: click compare button to toggle tolerance map on target.
 *
 * Supports zoom and pan:
 *   - Mouse wheel to zoom, drag to pan, double-click to reset.
 *   - By default, zoom/pan syncs across all images (linked mode).
 *   - Hold Ctrl to zoom/pan only the current image (independent mode).
 */
class ComparePanel : public QWidget
{
    Q_OBJECT

public:
    enum CompareMode {
        SwapMode,       ///< Press-hold to preview source image on target
        ToleranceMode   ///< Click to toggle tolerance map
    };

    explicit ComparePanel(CompareSession *session,
                          SettingsManager *settingsManager,
                          ImageLoader *imageLoader = nullptr,
                          QWidget *parent = nullptr);
    ~ComparePanel() override;

    void setSelectedImages(const QList<QPair<QString, QString>> &selectedImages);
    void clear();
    void setImageMarkManager(ImageMarkManager *manager);

    CompareMode compareMode() const { return m_compareMode; }

signals:
    /**
     * @brief Request navigation to the previous image set.
     */
    void navigatePreviousRequested();

    /**
     * @brief Request navigation to the next image set.
     */
    void navigateNextRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onFolderAdded(const QString &folderPath, int index);
    void onFolderRemoved(const QString &folderPath, int index);
    void onSessionCleared();
    void onComparePressed(int sourceIndex, int targetIndex);
    void onCompareReleased(int sourceIndex, int targetIndex);
    void onCompareClicked(int sourceIndex, int targetIndex);
    void onThresholdChanged(int value);
    void onModeToggled();
    void onResizeToFirstImageToggled(bool enabled);

    // Zoom/pan sync slots
    void onCellZoomChanged(double zoomLevel, QPointF focalPoint);
    void onCellPanChanged(QPointF offset);
    void onCellViewReset();
    void onImageReady(const QString &imagePath, const QImage &image);
    void onThumbnailReady(const QString &imagePath, const QImage &thumbnail);
    void onMarkChanged(const QString &folderPath,
                       const QString &imagePath,
                       const QString &category);

private:
    struct ImageCell {
        QWidget *container = nullptr;
        QWidget *imageContainer = nullptr;
        QLabel *headerLabel = nullptr;
        QWidget *markButtonsContainer = nullptr;
        QHBoxLayout *markButtonsLayout = nullptr;
        QList<QPushButton *> markButtons;
        QPushButton *renameButton = nullptr;
        QWidget *compareButtonsContainer = nullptr;
        QHBoxLayout *compareButtonsLayout = nullptr;
        QList<QPushButton *> compareButtons;
        ZoomableImageWidget *imageWidget = nullptr;
        QString folderPath;
        QString imagePath;
        QString customDisplayName;
        QImage originalImage;
        QImage previewImage;
        QImage cachedToleranceImage;   // Cached tolerance map (full res)
        bool hasImage = false;
        bool showingPreview = false;
        bool showingToleranceMap = false;
        int toleranceSourceIndex = -1;
    };

    void setupUi();
    ImageCell createCell(const QString &folderPath);
    void clearCells();
    void rebuildGrid();
    void setupCompareButtonsForCell(int cellIndex);
    void setupMarkButtonsForCell(int cellIndex);
    void positionMarkButtonsForCell(int cellIndex);
    void updateMarkButtonsForCell(int cellIndex);
    void updateAllMarkButtons();
    void markCell(int cellIndex, const QString &category);
    void markAllCurrentImages(const QString &category);
    QString markForCell(int cellIndex) const;
    void loadImage(int cellIndex);
    void preloadImagesForSelection(const QList<QPair<QString, QString>> &selectedImages);
    void clearImage(int cellIndex);
    void showPreviewImage(int cellIndex, const QImage &preview, bool resetView = false);
    void showOriginalImage(int cellIndex, bool resetView = false);
    void showToleranceMap(int sourceIndex, int targetIndex);
    void showSourceOnTarget(int sourceIndex, int targetIndex);
    void resizeImageCell(int cellIndex);
    void rebuildCompareButtons();
    void renameCell(int cellIndex);
    void updateCellHeader(int cellIndex);
    QString cellDisplayName(int cellIndex) const;
    QImage imageForCompare(int cellIndex) const;

    /**
     * @brief Find the cell index by its ZoomableImageWidget pointer.
     * @return Index, or -1 if not found.
     */
    int findCellByWidget(QObject *widget) const;

    CompareSession *m_session = nullptr;
    ImageLoader *m_imageLoader = nullptr;
    ImageMarkManager *m_markManager = nullptr;
    CompareMode m_compareMode = SwapMode;
    QToolBar *m_toolBar = nullptr;
    QAction *m_prevAction = nullptr;
    QAction *m_nextAction = nullptr;
    QAction *m_modeAction = nullptr;
    QSlider *m_thresholdSlider = nullptr;
    QLabel *m_thresholdValueLabel = nullptr;
    QWidget *m_thresholdContainer = nullptr; // to show/hide threshold controls
    QCheckBox *m_resizeToFirstImageCheckBox = nullptr;
    QWidget *m_gridContainer = nullptr;
    QGridLayout *m_gridLayout = nullptr;
    QList<ImageCell> m_cells;
    SettingsManager *m_settingsManager = nullptr;
    int m_threshold = 10;
    bool m_resizeToFirstImageEnabled = false;
    bool m_syncingViews = false; ///< Guard to prevent recursive sync loops
};

#endif // COMPAREPANEL_H
