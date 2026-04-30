#include "ImageListModel.h"
#include "services/ImageLoader.h"
#include "services/ImageMarkManager.h"
#include "utils/FileUtils.h"

#include <QDir>
#include <QFileInfo>
#include <QtConcurrent>
#include <QMetaObject>

ImageListModel::ImageListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

ImageListModel::~ImageListModel()
{
    cancelPendingScan();
}

int ImageListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_imagePaths.size();
}

QVariant ImageListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_imagePaths.size()) {
        return QVariant();
    }

    const QString &path = m_imagePaths.at(index.row());

    switch (role) {
    case Qt::DisplayRole:
    case FileNameRole:
        return m_fileNames.at(index.row());
    case FilePathRole:
        return path;
    case Qt::DecorationRole:
    case ThumbnailRole: {
        auto it = m_thumbnails.constFind(path);
        if (it != m_thumbnails.constEnd()) {
            return *it;
        }
        return QVariant();
    }
    case IsSelectedRole:
        return m_selectedIndices.contains(index.row());
    case MarkRole:
        return markAt(index.row());
    default:
        return QVariant();
    }
}

void ImageListModel::setFolder(const QString &folderPath)
{
    if (m_folderPath == folderPath) {
        return;
    }

    cancelPendingScan();

    beginResetModel();
    m_folderPath = folderPath;
    m_imagePaths.clear();
    m_pathToIndex.clear();
    m_fileNames.clear();
    m_fileNameToIndex.clear();
    m_selectedIndices.clear();
    m_thumbnails.clear();
    m_nextLoadIndex = 0;
    endResetModel();

    if (m_markManager) {
        m_markManager->loadFolder(folderPath);
    }

    startScan(folderPath);
}

QString ImageListModel::folderPath() const
{
    return m_folderPath;
}

QString ImageListModel::folderName() const
{
    return QDir(m_folderPath).dirName();
}

void ImageListModel::refresh()
{
    cancelPendingScan();

    beginResetModel();
    m_imagePaths.clear();
    m_pathToIndex.clear();
    m_fileNames.clear();
    m_fileNameToIndex.clear();
    m_selectedIndices.clear();
    m_thumbnails.clear();
    m_nextLoadIndex = 0;
    endResetModel();

    if (m_folderPath.isEmpty()) {
        return;
    }

    startScan(m_folderPath);
}

bool ImageListModel::isLoading() const
{
    return m_loading;
}

QString ImageListModel::imagePathAt(int index) const
{
    if (index < 0 || index >= m_imagePaths.size()) {
        return QString();
    }
    return m_imagePaths.at(index);
}

QString ImageListModel::fileNameAt(int index) const
{
    if (index < 0 || index >= m_imagePaths.size()) {
        return QString();
    }
    return m_fileNames.at(index);
}

int ImageListModel::imageCount() const
{
    return m_imagePaths.size();
}

int ImageListModel::indexOfFileName(const QString &fileName) const
{
    return m_fileNameToIndex.value(fileName, -1);
}

void ImageListModel::setSelected(int index, bool selected)
{
    if (index < 0 || index >= m_imagePaths.size()) {
        return;
    }

    bool changed = false;
    if (selected && !m_selectedIndices.contains(index)) {
        m_selectedIndices.insert(index);
        changed = true;
    } else if (!selected && m_selectedIndices.contains(index)) {
        m_selectedIndices.remove(index);
        changed = true;
    }

    if (changed) {
        QModelIndex mi = this->index(index);
        emit dataChanged(mi, mi, {IsSelectedRole});
        emit selectionChanged();
    }
}

void ImageListModel::clearSelection()
{
    if (m_selectedIndices.isEmpty()) {
        return;
    }

    QList<int> indices = m_selectedIndices.values();
    m_selectedIndices.clear();

    for (int idx : indices) {
        QModelIndex mi = this->index(idx);
        emit dataChanged(mi, mi, {IsSelectedRole});
    }
    emit selectionChanged();
}

QList<int> ImageListModel::selectedIndices() const
{
    return m_selectedIndices.values();
}

bool ImageListModel::isSelected(int index) const
{
    return m_selectedIndices.contains(index);
}

void ImageListModel::setImageMarkManager(ImageMarkManager *manager)
{
    if (m_markManager) {
        disconnect(m_markManager, nullptr, this, nullptr);
    }

    m_markManager = manager;

    if (m_markManager) {
        connect(m_markManager, &ImageMarkManager::markChanged,
                this, &ImageListModel::onMarkChanged);
        if (!m_folderPath.isEmpty()) {
            m_markManager->loadFolder(m_folderPath);
        }
    }

    if (!m_imagePaths.isEmpty()) {
        emit dataChanged(index(0), index(m_imagePaths.size() - 1), {MarkRole});
    }
}

QString ImageListModel::markAt(int index) const
{
    if (!m_markManager || index < 0 || index >= m_imagePaths.size()) {
        return QString();
    }

    return m_markManager->markForImage(m_folderPath, m_imagePaths.at(index));
}

bool ImageListModel::setMarkAt(int index, const QString &category)
{
    if (!m_markManager || index < 0 || index >= m_imagePaths.size()) {
        return false;
    }

    return m_markManager->setMarkForImage(m_folderPath, m_imagePaths.at(index), category);
}

void ImageListModel::setImageLoader(ImageLoader *loader)
{
    if (m_imageLoader) {
        disconnect(m_imageLoader, nullptr, this, nullptr);
    }

    m_imageLoader = loader;

    if (m_imageLoader) {
        connect(m_imageLoader, &ImageLoader::thumbnailReady,
                this, &ImageListModel::onThumbnailReady);
    }
}

void ImageListModel::loadThumbnailsForRange(int firstVisible, int lastVisible)
{
    if (!m_imageLoader) {
        return;
    }

    firstVisible = qMax(0, firstVisible);
    lastVisible = qMin(m_imagePaths.size() - 1, lastVisible);

    QStringList visibleFirst;
    visibleFirst.reserve(lastVisible - firstVisible + 1);
    for (int i = firstVisible; i <= lastVisible; ++i) {
        const QString &path = m_imagePaths.at(i);
        if (!m_thumbnails.contains(path)) {
            visibleFirst.append(path);
        }
    }

    if (!visibleFirst.isEmpty()) {
        m_imageLoader->requestThumbnailBatchVisibleFirst(visibleFirst);
    }
}

bool ImageListModel::loadNextThumbnailBatch(int batchSize)
{
    if (!m_imageLoader || m_nextLoadIndex >= m_imagePaths.size()) {
        return false;
    }

    int end = qMin(m_nextLoadIndex + batchSize, m_imagePaths.size());
    QStringList pathsToLoad;
    pathsToLoad.reserve(end - m_nextLoadIndex);
    for (int i = m_nextLoadIndex; i < end; ++i) {
        const QString &path = m_imagePaths.at(i);
        if (!m_thumbnails.contains(path)) {
            pathsToLoad.append(path);
        }
    }
    if (!pathsToLoad.isEmpty()) {
        m_imageLoader->requestThumbnailBatch(pathsToLoad);
    }
    m_nextLoadIndex = end;
    return m_nextLoadIndex < m_imagePaths.size();
}

bool ImageListModel::hasMoreToLoad() const
{
    return m_nextLoadIndex < m_imagePaths.size();
}

void ImageListModel::onThumbnailReady(const QString &imagePath, const QImage &thumbnail)
{
    auto it = m_pathToIndex.constFind(imagePath);
    if (it == m_pathToIndex.constEnd()) {
        return;
    }
    int idx = it.value();

    m_thumbnails.insert(imagePath, thumbnail);

    QModelIndex mi = this->index(idx);
    emit dataChanged(mi, mi, {Qt::DecorationRole, ThumbnailRole});
}

void ImageListModel::onMarkChanged(const QString &folderPath,
                                   const QString &imagePath,
                                   const QString &category)
{
    Q_UNUSED(category);

    const QString currentFolder = QDir::cleanPath(QFileInfo(m_folderPath).absoluteFilePath());
    const QString changedFolder = QDir::cleanPath(QFileInfo(folderPath).absoluteFilePath());
    if (currentFolder != changedFolder) {
        return;
    }

    const QString normalizedImage = QDir::cleanPath(QFileInfo(imagePath).absoluteFilePath());
    int idx = m_pathToIndex.value(normalizedImage, -1);
    if (idx < 0) {
        idx = m_pathToIndex.value(imagePath, -1);
    }
    if (idx < 0) {
        return;
    }

    QModelIndex mi = this->index(idx);
    emit dataChanged(mi, mi, {MarkRole});
}

void ImageListModel::startScan(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }

    m_loading = true;
    m_nextLoadIndex = 0;
    m_initialPrefetchRemaining = 300;
    ++m_scanGeneration;
    const int generation = m_scanGeneration;
    m_scanCancelToken = std::make_shared<FileUtils::ScanCancelToken>();
    emit scanProgressChanged(0, false);

    auto cancelToken = m_scanCancelToken;
    [[maybe_unused]] const auto future = QtConcurrent::run([this, path, generation, cancelToken]() {
        FileUtils::ScanOptions options;
        options.recursive = false;
        options.batchSize = 1000;
        options.initialBatchSize = 300;

        FileUtils::scanForImagesBatched(
            path,
            options,
            [this, generation](const QStringList &batch, bool /*initialBatch*/) {
                if (batch.isEmpty()) {
                    return;
                }
                QMetaObject::invokeMethod(this, [this, batch, generation]() {
                    appendScanBatch(batch, generation);
                }, Qt::QueuedConnection);
            },
            [this, generation](const FileUtils::ScanProgress &progress) {
                QMetaObject::invokeMethod(this, [this, progress, generation]() {
                    if (generation != m_scanGeneration) {
                        return;
                    }
                    emit scanProgressChanged(progress.discoveredCount, progress.finished);
                    if (progress.finished) {
                        finalizeScan(generation);
                    }
                }, Qt::QueuedConnection);
            },
            cancelToken);
    });
}

void ImageListModel::appendScanBatch(const QStringList &batch, int generation)
{
    if (generation != m_scanGeneration || batch.isEmpty()) {
        return;
    }

    const int beginRow = m_imagePaths.size();
    const int endRow = beginRow + batch.size() - 1;
    m_imagePaths.reserve(m_imagePaths.size() + batch.size());
    m_fileNames.reserve(m_fileNames.size() + batch.size());
    m_pathToIndex.reserve(m_pathToIndex.size() + batch.size());
    m_fileNameToIndex.reserve(m_fileNameToIndex.size() + batch.size());
    beginInsertRows(QModelIndex(), beginRow, endRow);
    for (const QString &path : batch) {
        const int index = m_imagePaths.size();
        const QString fileName = QFileInfo(path).fileName();
        m_pathToIndex.insert(path, index);
        if (!m_fileNameToIndex.contains(fileName)) {
            m_fileNameToIndex.insert(fileName, index);
        }
        m_imagePaths.append(path);
        m_fileNames.append(fileName);
    }
    endInsertRows();

    if (m_imageLoader && m_initialPrefetchRemaining > 0) {
        const int count = qMin(m_initialPrefetchRemaining, batch.size());
        m_imageLoader->requestThumbnailBatchVisibleFirst(batch.mid(0, count));
        m_initialPrefetchRemaining -= count;
    }
}

void ImageListModel::finalizeScan(int generation)
{
    if (generation != m_scanGeneration) {
        return;
    }

    if (m_scanCancelToken && m_scanCancelToken->isCancelled()) {
        return;
    }

    m_loading = false;
    m_nextLoadIndex = 0;
    emit folderReady();
}

void ImageListModel::cancelPendingScan()
{
    if (m_scanCancelToken) {
        m_scanCancelToken->cancel();
        m_scanCancelToken.reset();
    }

    if (m_loading) {
        m_loading = false;
    }
}
