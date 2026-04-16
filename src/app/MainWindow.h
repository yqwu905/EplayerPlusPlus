#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QList>

class QSplitter;
class QAction;
class FolderPanel;
class BrowsePanel;
class ComparePanel;
class SettingsManager;
class CompareSession;
class ImageLoader;

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
    void setupConnections();
    void togglePanel(int panelIndex);
    void saveSplitterSizes();

    QSplitter *m_mainSplitter = nullptr;

    // Services
    SettingsManager *m_settingsManager = nullptr;
    CompareSession *m_compareSession = nullptr;
    ImageLoader *m_imageLoader = nullptr;

    // Panels
    FolderPanel *m_folderPanel = nullptr;
    BrowsePanel *m_browsePanel = nullptr;

    // View menu toggle actions
    QAction *m_toggleFolderPanelAction = nullptr;
    QAction *m_toggleBrowsePanelAction = nullptr;

    // Saved sizes for restoring collapsed panels
    QList<int> m_savedSplitterSizes;
    ComparePanel *m_comparePanel = nullptr;
};

#endif // MAINWINDOW_H
