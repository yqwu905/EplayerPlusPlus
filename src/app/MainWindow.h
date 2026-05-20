#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QList>

class QSplitter;
class QAction;
class QSlider;
class QLabel;
class FolderPanel;
class BrowsePanel;
class ComparePanel;
class SettingsManager;
class CompareSession;
class ImageLoader;
class ImageMarkManager;

/**
 * @brief The MainWindow class
 *
 * Main application window with a three-panel layout:
 *   Left:   Folder management panel (FolderPanel)
 *   Center: Image browsing panel (BrowsePanel)
 *   Right:  Image comparison panel (ComparePanel)
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    void setupUi();
    void setupMenuBar();
    QWidget *createCommandBar();
    QAction *addCommandAction(const QString &text,
                              const QString &toolTip,
                              const QObject *receiver,
                              const char *member);
    void setupConnections();
    void exportCategoriesForFolder(const QString &folderPath);
    void togglePanel(int panelIndex);
    void saveSplitterSizes();
    void updateCompareModeActions();

    QSplitter *m_mainSplitter = nullptr;

    // Services
    SettingsManager *m_settingsManager = nullptr;
    CompareSession *m_compareSession = nullptr;
    ImageLoader *m_imageLoader = nullptr;
    ImageMarkManager *m_imageMarkManager = nullptr;

    // Panels
    FolderPanel *m_folderPanel = nullptr;
    BrowsePanel *m_browsePanel = nullptr;

    // View menu toggle actions
    QAction *m_toggleFolderPanelAction = nullptr;
    QAction *m_toggleBrowsePanelAction = nullptr;
    QAction *m_swapModeAction = nullptr;
    QAction *m_toleranceModeAction = nullptr;
    QAction *m_resizeToFirstAction = nullptr;
    QLabel *m_thresholdValueLabel = nullptr;
    QSlider *m_thresholdSlider = nullptr;

    // Saved sizes for restoring collapsed panels
    QList<int> m_savedSplitterSizes;
    ComparePanel *m_comparePanel = nullptr;
};

#endif // MAINWINDOW_H
