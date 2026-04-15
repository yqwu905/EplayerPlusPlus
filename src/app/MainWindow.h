#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class QSplitter;
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

    QSplitter *m_mainSplitter = nullptr;

    // Services
    SettingsManager *m_settingsManager = nullptr;
    CompareSession *m_compareSession = nullptr;
    ImageLoader *m_imageLoader = nullptr;

    // Panels
    FolderPanel *m_folderPanel = nullptr;
    BrowsePanel *m_browsePanel = nullptr;
    ComparePanel *m_comparePanel = nullptr;
};

#endif // MAINWINDOW_H
