#include "FolderModel.h"
#include "utils/FileUtils.h"

#include <QDir>
#include <QFileInfo>
#include <QtConcurrent>

#include <utility>

FolderModel::FolderModel(QObject *parent)
    : QAbstractItemModel(parent)
{
}

FolderModel::~FolderModel()
{
    cancelAllWatchers();
    qDeleteAll(m_rootNodes);
}

// ---- QAbstractItemModel interface ----

QModelIndex FolderModel::index(int row, int column, const QModelIndex &parent) const
{
    if (column != 0) {
        return QModelIndex();
    }

    if (!parent.isValid()) {
        // Root level
        if (row >= 0 && row < m_rootNodes.size()) {
            return createIndex(row, 0, m_rootNodes[row]);
        }
        return QModelIndex();
    }

    FolderNode *parentNode = nodeFromIndex(parent);
    if (!parentNode || row < 0 || row >= parentNode->children.size()) {
        return QModelIndex();
    }

    return createIndex(row, 0, parentNode->children[row]);
}

QModelIndex FolderModel::parent(const QModelIndex &child) const
{
    if (!child.isValid()) {
        return QModelIndex();
    }

    FolderNode *childNode = nodeFromIndex(child);
    if (!childNode || !childNode->parent) {
        return QModelIndex();
    }

    FolderNode *parentNode = childNode->parent;

    // Find the row of the parent
    if (!parentNode->parent) {
        // Parent is a root node
        const int row = parentNode->rowInParent;
        if (row < 0 || row >= m_rootNodes.size()) {
            return QModelIndex();
        }
        return createIndex(row, 0, parentNode);
    }

    // Parent is a child of some other node
    const int row = parentNode->rowInParent;
    if (row < 0 || row >= parentNode->parent->children.size()) {
        return QModelIndex();
    }
    return createIndex(row, 0, parentNode);
}

int FolderModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid()) {
        return m_rootNodes.size();
    }

    FolderNode *node = nodeFromIndex(parent);
    return node ? node->children.size() : 0;
}

int FolderModel::columnCount(const QModelIndex & /*parent*/) const
{
    return 1;
}

QVariant FolderModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }

    FolderNode *node = nodeFromIndex(index);
    if (!node) {
        return QVariant();
    }

    switch (role) {
    case Qt::DisplayRole:
        return node->displayName;
    case Qt::DecorationRole:
        return m_iconProvider.icon(QFileIconProvider::Folder);
    case Qt::ToolTipRole:
        return node->path;
    case PathRole:
        return node->path;
    case IsRootRole:
        return (node->parent == nullptr);
    default:
        return QVariant();
    }
}

bool FolderModel::hasChildren(const QModelIndex &parent) const
{
    if (!parent.isValid()) {
        return !m_rootNodes.isEmpty();
    }

    FolderNode *node = nodeFromIndex(parent);
    if (!node) {
        return false;
    }

    // Optimistic strategy: assume unfetched nodes have children.
    // This avoids synchronous disk I/O (which blocks the GUI on network folders).
    // The expand arrow will disappear after fetch if no children are found.
    if (!node->fetched) {
        return true;
    }

    return !node->children.isEmpty();
}

bool FolderModel::canFetchMore(const QModelIndex &parent) const
{
    if (!parent.isValid()) {
        return false;
    }

    FolderNode *node = nodeFromIndex(parent);
    return node && !node->fetched && !node->fetching;
}

void FolderModel::fetchMore(const QModelIndex &parent)
{
    if (!parent.isValid()) {
        return;
    }

    FolderNode *node = nodeFromIndex(parent);
    if (!node || node->fetched || node->fetching) {
        return;
    }

    node->fetching = true;
    emit fetchStarted(parent);

    // Capture the path for the worker thread (safe to use since path is immutable)
    QString path = node->path;

    // Run directory scan in a background thread
    QFuture<QStringList> future = QtConcurrent::run([path]() {
        return FileUtils::getSubdirectories(path);
    });

    auto *watcher = new QFutureWatcher<QStringList>(this);
    m_activeWatchers.insert(node, watcher);
    const quint64 nodeGeneration = node->generation;

    connect(watcher, &QFutureWatcher<QStringList>::finished, this,
            [this, node, nodeGeneration, watcher]() {
        QStringList subdirs = watcher->result();
        auto activeIt = m_activeWatchers.find(node);
        if (activeIt != m_activeWatchers.end() && activeIt.value() == watcher) {
            m_activeWatchers.erase(activeIt);
        }
        watcher->deleteLater();
        onFetchFinished(node, nodeGeneration, subdirs);
    });

    watcher->setFuture(future);
}

// ---- Folder management API ----

bool FolderModel::addFolder(const QString &path)
{
    QFileInfo fi(path);
    if (!fi.exists() || !fi.isDir()) {
        return false;
    }

    QString absPath = fi.absoluteFilePath();

    // Check for duplicates
    for (const FolderNode *node : m_rootNodes) {
        if (node->path == absPath) {
            return false;
        }
    }

    int row = m_rootNodes.size();
    beginInsertRows(QModelIndex(), row, row);

    auto *node = new FolderNode();
    node->path = absPath;
    node->displayName = QDir(absPath).dirName();
    node->parent = nullptr;
    node->rowInParent = row;
    node->generation = m_nextNodeGeneration++;
    m_rootNodes.append(node);
    m_liveNodes.insert(node);

    endInsertRows();
    return true;
}

bool FolderModel::removeFolder(const QModelIndex &index)
{
    if (!index.isValid()) {
        return false;
    }

    FolderNode *node = nodeFromIndex(index);
    if (!node || node->parent != nullptr) {
        // Can only remove root folders
        return false;
    }

    const int row = node->rowInParent;
    if (row < 0) {
        return false;
    }

    // Cancel any in-flight fetch for this folder or its expanded descendants
    // before freeing them. A per-node generation also protects against address
    // reuse (ABA), so stale async results cannot populate a replacement node.
    cancelWatchersForSubtree(node);

    beginRemoveRows(QModelIndex(), row, row);
    m_rootNodes.removeAt(row);
    unregisterSubtree(node);
    delete node;
    for (int i = row; i < m_rootNodes.size(); ++i) {
        m_rootNodes[i]->rowInParent = i;
    }
    endRemoveRows();

    return true;
}

void FolderModel::refreshFolder(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }

    FolderNode *node = nodeFromIndex(index);
    if (!node) {
        return;
    }

    // Refresh deletes the complete descendant subtree, so detach every watcher
    // in it before any node address can be reused.
    cancelWatchersForSubtree(node);

    // Clear and re-fetch children
    if (!node->children.isEmpty()) {
        beginRemoveRows(index, 0, node->children.size() - 1);
        clearChildren(node);
        endRemoveRows();
    }

    node->fetched = false;
    node->fetching = false;

    // Notify views that this node might have children again
    emit dataChanged(index, index);
}

void FolderModel::refreshAll()
{
    for (int i = 0; i < m_rootNodes.size(); ++i) {
        refreshFolder(index(i, 0));
    }
}

void FolderModel::clearAll()
{
    if (m_rootNodes.isEmpty()) {
        return;
    }

    cancelAllWatchers();

    beginResetModel();
    m_liveNodes.clear();
    qDeleteAll(m_rootNodes);
    m_rootNodes.clear();
    endResetModel();
}

QString FolderModel::filePath(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return QString();
    }

    FolderNode *node = nodeFromIndex(index);
    return node ? node->path : QString();
}

bool FolderModel::isRootFolder(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return false;
    }

    FolderNode *node = nodeFromIndex(index);
    return node && (node->parent == nullptr);
}

QStringList FolderModel::rootFolderPaths() const
{
    QStringList paths;
    for (const FolderNode *node : m_rootNodes) {
        paths << node->path;
    }
    return paths;
}

void FolderModel::setRootFolders(const QStringList &paths)
{
    cancelAllWatchers();
    beginResetModel();
    m_liveNodes.clear();
    qDeleteAll(m_rootNodes);
    m_rootNodes.clear();

    for (const QString &path : paths) {
        QFileInfo fi(path);
        if (fi.exists() && fi.isDir()) {
            auto *node = new FolderNode();
            node->path = fi.absoluteFilePath();
            node->displayName = QDir(node->path).dirName();
            node->parent = nullptr;
            node->rowInParent = m_rootNodes.size();
            node->generation = m_nextNodeGeneration++;
            m_rootNodes.append(node);
            m_liveNodes.insert(node);
        }
    }

    endResetModel();
}

// ---- Private helpers ----

FolderModel::FolderNode *FolderModel::nodeFromIndex(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return nullptr;
    }
    return static_cast<FolderNode *>(index.internalPointer());
}

QModelIndex FolderModel::indexFromNode(FolderNode *node) const
{
    if (!node) {
        return QModelIndex();
    }

    if (!node->parent) {
        const int row = node->rowInParent;
        return (row >= 0) ? createIndex(row, 0, node) : QModelIndex();
    }

    const int row = node->rowInParent;
    return (row >= 0) ? createIndex(row, 0, node) : QModelIndex();
}

void FolderModel::populateChildren(FolderNode *node, const QStringList &subdirs)
{
    if (!node) {
        return;
    }

    if (!subdirs.isEmpty()) {
        QModelIndex parentIndex = indexFromNode(node);
        beginInsertRows(parentIndex, 0, subdirs.size() - 1);

        for (const QString &subdir : subdirs) {
            auto *child = new FolderNode();
            child->path = subdir;
            child->displayName = QDir(subdir).dirName();
            child->parent = node;
            child->rowInParent = node->children.size();
            child->generation = m_nextNodeGeneration++;
            node->children.append(child);
            m_liveNodes.insert(child);
        }

        endInsertRows();
    }

    node->fetched = true;
    node->fetching = false;
}

void FolderModel::clearChildren(FolderNode *node)
{
    for (FolderNode *child : std::as_const(node->children)) {
        unregisterSubtree(child);
    }
    qDeleteAll(node->children);
    node->children.clear();
    node->fetched = false;
    node->fetching = false;
}

void FolderModel::unregisterSubtree(FolderNode *node)
{
    if (!node) {
        return;
    }
    for (FolderNode *child : std::as_const(node->children)) {
        unregisterSubtree(child);
    }
    m_liveNodes.remove(node);
}

void FolderModel::onFetchFinished(FolderNode *node, quint64 generation,
                                  const QStringList &subdirs)
{
    // O(1) lifetime validation. A DFS over the whole tree on every completed
    // directory fetch made broad expanded trees increasingly expensive.
    if (!m_liveNodes.contains(node) || node->generation != generation) {
        return;  // Node was deleted while async fetch was running
    }

    populateChildren(node, subdirs);

    QModelIndex idx = indexFromNode(node);
    emit fetchFinished(idx);
}

void FolderModel::cancelWatchersForSubtree(FolderNode *node)
{
    if (!node) {
        return;
    }
    if (auto *watcher = m_activeWatchers.take(node)) {
        // Detach the finished-slot first so it can't run against the node we are
        // about to delete, then cancel/destroy the watcher.
        watcher->disconnect();
        watcher->cancel();
        watcher->deleteLater();
    }
    for (FolderNode *child : node->children) {
        cancelWatchersForSubtree(child);
    }
}

void FolderModel::cancelAllWatchers()
{
    for (auto it = m_activeWatchers.begin(); it != m_activeWatchers.end(); ++it) {
        it.value()->disconnect();
        it.value()->cancel();
        it.value()->deleteLater();
    }
    m_activeWatchers.clear();
}
