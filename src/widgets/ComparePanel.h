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
class ArrowOverlay;
class SettingsManager;
class CompareSession;

/**
 * @brief Image comparison panel displaying selected images in a grid.
 *
 * Maintains one cell per folder in the CompareSession. Two arrow modes:
 *   - Swap mode (default): press-hold arrow to preview source on target,
 *     release to restore.
 *   - Tolerance mode: click arrow to toggle tolerance map on target.
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
                          QWidget *parent = nullptr);
    ~ComparePanel() override;

    void setSelectedImages(const QList<QPair<QString, QString>> &selectedImages);
    void clear();

    CompareMode compareMode() const { return m_compareMode; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onFolderAdded(const QString &folderPath, int index);
    void onFolderRemoved(const QString &folderPath, int index);
    void onSessionCleared();
    void onArrowPressed(int sourceIndex, int targetIndex);
    void onArrowReleased(int sourceIndex, int targetIndex);
    void onArrowClicked(int sourceIndex, int targetIndex);
    void onThresholdChanged(int value);
    void onModeToggled();

private:
    struct ImageCell {
        QWidget *container = nullptr;
        QWidget *imageContainer = nullptr;
        QLabel *headerLabel = nullptr;
        QLabel *imageLabel = nullptr;
        ArrowOverlay *arrowOverlay = nullptr;
        QString folderPath;
        QString imagePath;
        QImage originalImage;
        QPixmap cachedOriginalPixmap;  // Pre-scaled for display
        QImage cachedToleranceImage;   // Cached tolerance map (full res)
        QPixmap cachedTolerancePixmap; // Pre-scaled tolerance for display
        QSize cachedDisplaySize;       // Size used for cached pixmaps
        bool hasImage = false;
        bool showingToleranceMap = false;
        int toleranceSourceIndex = -1;
    };

    void setupUi();
    ImageCell createCell(const QString &folderPath);
    void clearCells();
    void rebuildGrid();
    void setupArrowsForCell(int cellIndex);
    void loadImage(int cellIndex);
    void clearImage(int cellIndex);
    void showOriginalImage(int cellIndex);
    void showToleranceMap(int sourceIndex, int targetIndex);
    void showSourceOnTarget(int sourceIndex, int targetIndex);
    void resizeImageCell(int cellIndex);
    void reconnectArrows();

    CompareSession *m_session = nullptr;
    CompareMode m_compareMode = SwapMode;
    QToolBar *m_toolBar = nullptr;
    QAction *m_modeAction = nullptr;
    QSlider *m_thresholdSlider = nullptr;
    QLabel *m_thresholdValueLabel = nullptr;
    QWidget *m_thresholdContainer = nullptr; // to show/hide threshold controls
    QWidget *m_gridContainer = nullptr;
    QGridLayout *m_gridLayout = nullptr;
    QList<ImageCell> m_cells;
    SettingsManager *m_settingsManager = nullptr;
    int m_threshold = 10;
};

#endif // COMPAREPANEL_H
