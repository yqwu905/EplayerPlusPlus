#ifndef FOLDERPANEL_H
#define FOLDERPANEL_H

#include <QWidget>

class QTreeView;
class QToolBar;
class FolderModel;
class SettingsManager;

/**
 * @brief Left-side folder management panel.
 *
 * Contains a QTreeView displaying user-added folders with their subdirectories,
 * and a toolbar with Add, Refresh, and Clear buttons.
 * Supports right-click context menu with Delete, Refresh, and Add to Compare actions.
 */
class FolderPanel : public QWidget
{
    Q_OBJECT

public:
    explicit FolderPanel(SettingsManager *settingsManager, QWidget *parent = nullptr);
    ~FolderPanel() override;

    /**
     * @brief Get the folder model.
     */
    FolderModel *folderModel() const { return m_folderModel; }

signals:
    /**
     * @brief Emitted when a folder is requested to be added to comparison.
     * @param path Absolute path of the folder.
     */
    void addToCompareRequested(const QString &path);

private slots:
    void onAddFolder();
    void onRefreshAll();
    void onClearAll();
    void onContextMenu(const QPoint &pos);

private:
    void setupUi();
    void setupConnections();
    void saveFolderList();
    void restoreFolderList();

    QTreeView *m_treeView = nullptr;
    QToolBar *m_toolBar = nullptr;
    FolderModel *m_folderModel = nullptr;
    SettingsManager *m_settingsManager = nullptr;
};

#endif // FOLDERPANEL_H
