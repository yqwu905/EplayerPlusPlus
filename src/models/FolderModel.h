#ifndef FOLDERMODEL_H
#define FOLDERMODEL_H

#include <QAbstractItemModel>
#include <QFileIconProvider>
#include <QStringList>
#include <QDir>
#include <QFutureWatcher>
#include <QMap>

/**
 * @brief Tree model for managing user-added folders and their subdirectories.
 *
 * Top-level items are user-added root folders. Expanding a folder lazily
 * loads its immediate subdirectories. This model does NOT use QFileSystemModel
 * because we only want to show user-selected roots and their children, not
 * the entire filesystem.
 */
class FolderModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Roles {
        PathRole = Qt::UserRole + 1,
        IsRootRole
    };

    explicit FolderModel(QObject *parent = nullptr);
    ~FolderModel() override;

    // ---- QAbstractItemModel interface ----
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool hasChildren(const QModelIndex &parent = QModelIndex()) const override;
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;

    // ---- Folder management API ----

    /**
     * @brief Add a root folder to the model.
     * @param path Absolute path to the folder.
     * @return true if added successfully, false if already exists or invalid.
     */
    bool addFolder(const QString &path);

    /**
     * @brief Remove a root folder from the model.
     * @param index Model index of the folder to remove (must be a root item).
     * @return true if removed successfully.
     */
    bool removeFolder(const QModelIndex &index);

    /**
     * @brief Refresh a specific folder (re-scan subdirectories).
     * @param index Model index of the folder to refresh.
     */
    void refreshFolder(const QModelIndex &index);

    /**
     * @brief Refresh all root folders.
     */
    void refreshAll();

    /**
     * @brief Remove all root folders.
     */
    void clearAll();

    /**
     * @brief Get the absolute path for a given model index.
     * @param index Model index.
     * @return Absolute path, or empty string if invalid.
     */
    QString filePath(const QModelIndex &index) const;

    /**
     * @brief Check if the given index is a root-level folder.
     * @param index Model index.
     * @return true if the index represents a user-added root folder.
     */
    bool isRootFolder(const QModelIndex &index) const;

    /**
     * @brief Get the list of all root folder paths.
     * @return List of absolute paths.
     */
    QStringList rootFolderPaths() const;

    /**
     * @brief Set root folders from a list (used for restoring from settings).
     * @param paths List of folder paths.
     */
    void setRootFolders(const QStringList &paths);

signals:
    /**
     * @brief Emitted when a folder is requested to be added to comparison.
     * @param path Absolute path of the folder.
     */
    void addToCompareRequested(const QString &path);

    /**
     * @brief Emitted when async fetch starts for a folder.
     * @param index Model index of the folder being fetched.
     */
    void fetchStarted(const QModelIndex &index);

    /**
     * @brief Emitted when async fetch completes for a folder.
     * @param index Model index of the folder that was fetched.
     */
    void fetchFinished(const QModelIndex &index);

private:
    struct FolderNode {
        QString path;           // Absolute path
        QString displayName;    // Folder name for display
        FolderNode *parent = nullptr;
        QList<FolderNode *> children;
        bool fetched = false;   // Whether subdirectories have been loaded
        bool fetching = false;  // Whether an async fetch is in progress

        ~FolderNode() { qDeleteAll(children); }
    };

    QList<FolderNode *> m_rootNodes;
    QFileIconProvider m_iconProvider;

    FolderNode *nodeFromIndex(const QModelIndex &index) const;
    QModelIndex indexFromNode(FolderNode *node) const;
    void populateChildren(FolderNode *node, const QStringList &subdirs);
    void clearChildren(FolderNode *node);

    void onFetchFinished(FolderNode *node, const QStringList &subdirs);
    void cancelAllWatchers();

    // Track active async watchers to prevent leaks and allow cancellation
    QMap<FolderNode *, QFutureWatcher<QStringList> *> m_activeWatchers;
};

#endif // FOLDERMODEL_H
