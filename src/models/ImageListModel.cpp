#include "ImageListModel.h"
#include "services/ImageLoader.h"
#include "services/ImageMarkManager.h"
#include "utils/FileUtils.h"

#include <QDir>
#include <QFileInfo>
#include <QtConcurrent>
#include <QMetaObject>

#include <algorithm>
#include <utility>

namespace
{
constexpr int kBrowseThumbnailExtent = 180;
const QSize kBrowseThumbnailSize(kBrowseThumbnailExtent, kBrowseThumbnailExtent);
const QString kUnmarkedCategoryFilter = QStringLiteral("__unmarked__");
}

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
    return imageCount();
}

QVariant ImageListModel::data(const QModelIndex &index, int role) const
{
    const int sourceIndex = sourceIndexForRow(index.row());
    if (!index.isValid() || sourceIndex < 0) {
        return QVariant();
    }

    const QString &path = m_imagePaths.at(sourceIndex);

    switch (role) {
    case Qt::DisplayRole:
    case FileNameRole:
        return m_fileNames.at(sourceIndex);
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
        return m_selectedIndices.contains(sourceIndex);
    case MarkRole:
        return markAtSourceIndex(sourceIndex);
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
    m_normalizedFolderPath = ImageMarkManager::normalizeFolderPath(folderPath);
    m_imagePaths.clear();
    m_pathToIndex.clear();
    m_markKeys.clear();
    m_markKeyToIndex.clear();
    m_fileNames.clear();
    m_fileNameToIndex.clear();
    m_filteredSourceRows.clear();
    m_selectedIndices.clear();
    m_thumbnails.clear();
    m_nextLoadIndex = 0;
    endResetModel();

    if (m_markManager) {
        m_markManager->loadFolder(m_normalizedFolderPath);
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
    m_markKeys.clear();
    m_markKeyToIndex.clear();
    m_fileNames.clear();
    m_fileNameToIndex.clear();
    m_filteredSourceRows.clear();
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
    const int sourceIndex = sourceIndexForRow(index);
    if (sourceIndex < 0) {
        return QString();
    }
    return m_imagePaths.at(sourceIndex);
}

QString ImageListModel::fileNameAt(int index) const
{
    const int sourceIndex = sourceIndexForRow(index);
    if (sourceIndex < 0) {
        return QString();
    }
    return m_fileNames.at(sourceIndex);
}

int ImageListModel::imageCount() const
{
    return hasActiveFilters() ? m_filteredSourceRows.size() : m_imagePaths.size();
}

int ImageListModel::unfilteredImageCount() const
{
    return m_imagePaths.size();
}

void ImageListModel::setFileNameFilter(const QString &filterText)
{
    const QString normalized = filterText.trimmed();
    if (m_fileNameFilter == normalized) {
        return;
    }

    m_fileNameFilter = normalized;
    applyFilters();
}

QString ImageListModel::fileNameFilter() const
{
    return m_fileNameFilter;
}

void ImageListModel::setCategoryFilter(const QString &category)
{
    const QString normalized = category.trimmed();
    if (!normalized.isEmpty() &&
        normalized != kUnmarkedCategoryFilter &&
        !ImageMarkManager::isValidCategory(normalized)) {
        return;
    }

    if (m_categoryFilter == normalized) {
        return;
    }

    m_categoryFilter = normalized;
    applyFilters();
}

QString ImageListModel::categoryFilter() const
{
    return m_categoryFilter;
}

bool ImageListModel::hasActiveFilters() const
{
    return !m_fileNameFilter.isEmpty() || !m_categoryFilter.isEmpty();
}

int ImageListModel::indexOfFileName(const QString &fileName) const
{
    if (hasActiveFilters()) {
        for (int row = 0; row < m_filteredSourceRows.size(); ++row) {
            const int sourceIndex = m_filteredSourceRows.at(row);
            if (sourceIndex >= 0 &&
                sourceIndex < m_fileNames.size() &&
                m_fileNames.at(sourceIndex) == fileName) {
                return row;
            }
        }
        return -1;
    }

    return m_fileNameToIndex.value(fileName, -1);
}

void ImageListModel::setSelected(int index, bool selected)
{
    const int sourceIndex = sourceIndexForRow(index);
    if (sourceIndex < 0) {
        return;
    }

    bool changed = false;
    if (selected && !m_selectedIndices.contains(sourceIndex)) {
        m_selectedIndices.insert(sourceIndex);
        changed = true;
    } else if (!selected && m_selectedIndices.contains(sourceIndex)) {
        m_selectedIndices.remove(sourceIndex);
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

    QList<int> indices;
    indices.reserve(m_selectedIndices.size());
    for (int sourceIndex : std::as_const(m_selectedIndices)) {
        const int row = rowForSourceIndex(sourceIndex);
        if (row >= 0) {
            indices.append(row);
        }
    }

    m_selectedIndices.clear();

    for (int idx : indices) {
        QModelIndex mi = this->index(idx);
        emit dataChanged(mi, mi, {IsSelectedRole});
    }
    emit selectionChanged();
}

QList<int> ImageListModel::selectedIndices() const
{
    QList<int> rows;
    rows.reserve(m_selectedIndices.size());
    for (int sourceIndex : std::as_const(m_selectedIndices)) {
        const int row = rowForSourceIndex(sourceIndex);
        if (row >= 0) {
            rows.append(row);
        }
    }
    return rows;
}

bool ImageListModel::isSelected(int index) const
{
    const int sourceIndex = sourceIndexForRow(index);
    return sourceIndex >= 0 && m_selectedIndices.contains(sourceIndex);
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
            m_markManager->loadFolder(m_normalizedFolderPath);
        }
    }

    if (!m_categoryFilter.isEmpty()) {
        applyFilters();
        return;
    }

    if (imageCount() > 0) {
        emit dataChanged(index(0), index(imageCount() - 1), {MarkRole});
    }
}

QString ImageListModel::markAt(int index) const
{
    return markAtSourceIndex(sourceIndexForRow(index));
}

bool ImageListModel::setMarkAt(int index, const QString &category)
{
    const int sourceIndex = sourceIndexForRow(index);
    if (!m_markManager || sourceIndex < 0) {
        return false;
    }

    return m_markManager->setMarkForImageKey(m_normalizedFolderPath,
                                             m_markKeys.at(sourceIndex),
                                             category);
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
    if (!m_imageLoader || imageCount() <= 0) {
        return;
    }

    firstVisible = qMax(0, firstVisible);
    lastVisible = qMin(imageCount() - 1, lastVisible);
    if (lastVisible < firstVisible) {
        return;
    }

    QStringList visibleFirst;
    visibleFirst.reserve(lastVisible - firstVisible + 1);
    for (int i = firstVisible; i <= lastVisible; ++i) {
        const QString path = imagePathAt(i);
        if (!m_thumbnails.contains(path)) {
            visibleFirst.append(path);
        }
    }

    if (!visibleFirst.isEmpty()) {
        m_imageLoader->requestThumbnailBatchVisibleFirst(visibleFirst, kBrowseThumbnailSize);
    }
}

bool ImageListModel::loadNextThumbnailBatch(int batchSize)
{
    if (!m_imageLoader || m_nextLoadIndex >= imageCount()) {
        return false;
    }

    int end = qMin(m_nextLoadIndex + batchSize, imageCount());
    QStringList pathsToLoad;
    pathsToLoad.reserve(end - m_nextLoadIndex);
    for (int i = m_nextLoadIndex; i < end; ++i) {
        const QString path = imagePathAt(i);
        if (!m_thumbnails.contains(path)) {
            pathsToLoad.append(path);
        }
    }
    if (!pathsToLoad.isEmpty()) {
        m_imageLoader->requestThumbnailBatch(pathsToLoad, kBrowseThumbnailSize);
    }
    m_nextLoadIndex = end;
    return m_nextLoadIndex < imageCount();
}

bool ImageListModel::hasMoreToLoad() const
{
    return m_nextLoadIndex < imageCount();
}

void ImageListModel::onThumbnailReady(const QString &imagePath, const QImage &thumbnail)
{
    if (thumbnail.width() > kBrowseThumbnailSize.width() ||
        thumbnail.height() > kBrowseThumbnailSize.height()) {
        return;
    }

    auto it = m_pathToIndex.constFind(imagePath);
    if (it == m_pathToIndex.constEnd()) {
        return;
    }
    const int sourceIndex = it.value();

    m_thumbnails.insert(imagePath, thumbnail);

    const int row = rowForSourceIndex(sourceIndex);
    if (row < 0) {
        return;
    }

    QModelIndex mi = this->index(row);
    emit dataChanged(mi, mi, {Qt::DecorationRole, ThumbnailRole});
}

void ImageListModel::onMarkChanged(const QString &folderPath,
                                   const QString &imagePath,
                                   const QString &category)
{
    Q_UNUSED(category);

    const QString changedFolder = ImageMarkManager::normalizeFolderPath(folderPath);
    if (m_normalizedFolderPath != changedFolder) {
        return;
    }

    const QString markKey = ImageMarkManager::imageKeyForPath(m_normalizedFolderPath, imagePath);
    int idx = m_markKeyToIndex.value(markKey, -1);
    if (idx < 0) {
        idx = m_pathToIndex.value(imagePath, -1);
    }
    if (idx < 0) {
        return;
    }

    updateFilteredRowForSourceIndex(idx);
}

void ImageListModel::startScan(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }

    m_loading = true;
    m_nextLoadIndex = 0;
    m_initialPrefetchRemaining = 0;
    ++m_scanGeneration;
    const int generation = m_scanGeneration;
    m_scanCancelToken = std::make_shared<FileUtils::ScanCancelToken>();
    emit scanProgressChanged(0, false);

    auto cancelToken = m_scanCancelToken;
    [[maybe_unused]] const auto future = QtConcurrent::run([this, path, generation, cancelToken]() {
        FileUtils::ScanOptions options;
        options.recursive = false;
        options.batchSize = 64;
        options.initialBatchSize = 24;

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

    m_imagePaths.reserve(m_imagePaths.size() + batch.size());
    m_fileNames.reserve(m_fileNames.size() + batch.size());
    m_pathToIndex.reserve(m_pathToIndex.size() + batch.size());
    m_markKeys.reserve(m_markKeys.size() + batch.size());
    m_markKeyToIndex.reserve(m_markKeyToIndex.size() + batch.size());
    m_fileNameToIndex.reserve(m_fileNameToIndex.size() + batch.size());
    m_filteredSourceRows.reserve(m_filteredSourceRows.size() + batch.size());

    if (!hasActiveFilters()) {
        const int beginRow = m_imagePaths.size();
        const int endRow = beginRow + batch.size() - 1;
        beginInsertRows(QModelIndex(), beginRow, endRow);
        for (const QString &path : batch) {
            const int index = m_imagePaths.size();
            const QString fileName = QFileInfo(path).fileName();
            const QString markKey = ImageMarkManager::imageKeyForPath(m_normalizedFolderPath, path);
            m_pathToIndex.insert(path, index);
            m_markKeys.append(markKey);
            if (!markKey.isEmpty()) {
                m_markKeyToIndex.insert(markKey, index);
            }
            if (!m_fileNameToIndex.contains(fileName)) {
                m_fileNameToIndex.insert(fileName, index);
            }
            m_imagePaths.append(path);
            m_fileNames.append(fileName);
        }
        endInsertRows();
    } else {
        for (const QString &path : batch) {
            const int sourceIndex = m_imagePaths.size();
            const QString fileName = QFileInfo(path).fileName();
            const QString markKey = ImageMarkManager::imageKeyForPath(m_normalizedFolderPath, path);
            m_pathToIndex.insert(path, sourceIndex);
            m_markKeys.append(markKey);
            if (!markKey.isEmpty()) {
                m_markKeyToIndex.insert(markKey, sourceIndex);
            }
            if (!m_fileNameToIndex.contains(fileName)) {
                m_fileNameToIndex.insert(fileName, sourceIndex);
            }
            m_imagePaths.append(path);
            m_fileNames.append(fileName);

            if (sourceImageMatchesFilters(sourceIndex)) {
                const int row = m_filteredSourceRows.size();
                beginInsertRows(QModelIndex(), row, row);
                m_filteredSourceRows.append(sourceIndex);
                endInsertRows();
            }
        }
    }

    if (m_imageLoader && m_initialPrefetchRemaining > 0) {
        QStringList candidates;
        if (!hasActiveFilters()) {
            candidates = batch;
        } else {
            for (int i = qMax(0, imageCount() - batch.size()); i < imageCount(); ++i) {
                const QString path = imagePathAt(i);
                if (!path.isEmpty()) {
                    candidates.append(path);
                }
            }
        }

        const int count = qMin(m_initialPrefetchRemaining, candidates.size());
        m_imageLoader->requestThumbnailBatchVisibleFirst(candidates.mid(0, count),
                                                         kBrowseThumbnailSize);
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

int ImageListModel::sourceIndexForRow(int row) const
{
    if (row < 0 || row >= imageCount()) {
        return -1;
    }

    return hasActiveFilters() ? m_filteredSourceRows.at(row) : row;
}

int ImageListModel::rowForSourceIndex(int sourceIndex) const
{
    if (sourceIndex < 0 || sourceIndex >= m_imagePaths.size()) {
        return -1;
    }

    if (!hasActiveFilters()) {
        return sourceIndex;
    }

    return m_filteredSourceRows.indexOf(sourceIndex);
}

bool ImageListModel::sourceImageMatchesFilters(int sourceIndex) const
{
    if (sourceIndex < 0 || sourceIndex >= m_imagePaths.size()) {
        return false;
    }

    if (!m_fileNameFilter.isEmpty() &&
        !m_fileNames.at(sourceIndex).contains(m_fileNameFilter, Qt::CaseInsensitive)) {
        return false;
    }

    if (!m_categoryFilter.isEmpty()) {
        const QString mark = markAtSourceIndex(sourceIndex);
        if (m_categoryFilter == kUnmarkedCategoryFilter) {
            return mark.isEmpty();
        }
        return mark == m_categoryFilter;
    }

    return true;
}

void ImageListModel::rebuildFilteredRows()
{
    m_filteredSourceRows.clear();
    if (!hasActiveFilters()) {
        return;
    }

    m_filteredSourceRows.reserve(m_imagePaths.size());
    for (int sourceIndex = 0; sourceIndex < m_imagePaths.size(); ++sourceIndex) {
        if (sourceImageMatchesFilters(sourceIndex)) {
            m_filteredSourceRows.append(sourceIndex);
        }
    }
}

void ImageListModel::applyFilters()
{
    beginResetModel();
    rebuildFilteredRows();
    m_nextLoadIndex = 0;
    endResetModel();
    emit selectionChanged();
}

void ImageListModel::updateFilteredRowForSourceIndex(int sourceIndex)
{
    if (sourceIndex < 0 || sourceIndex >= m_imagePaths.size()) {
        return;
    }

    if (!hasActiveFilters()) {
        const QModelIndex mi = index(sourceIndex);
        emit dataChanged(mi, mi, {MarkRole});
        return;
    }

    const int oldRow = m_filteredSourceRows.indexOf(sourceIndex);
    const bool matches = sourceImageMatchesFilters(sourceIndex);

    if (oldRow >= 0 && !matches) {
        beginRemoveRows(QModelIndex(), oldRow, oldRow);
        m_filteredSourceRows.removeAt(oldRow);
        endRemoveRows();
        emit selectionChanged();
        return;
    }

    if (oldRow < 0 && matches) {
        const auto insertIt = std::lower_bound(m_filteredSourceRows.cbegin(),
                                               m_filteredSourceRows.cend(),
                                               sourceIndex);
        const int newRow = static_cast<int>(std::distance(m_filteredSourceRows.cbegin(),
                                                          insertIt));
        beginInsertRows(QModelIndex(), newRow, newRow);
        m_filteredSourceRows.insert(newRow, sourceIndex);
        endInsertRows();
        emit selectionChanged();
        return;
    }

    if (oldRow >= 0) {
        const QModelIndex mi = index(oldRow);
        emit dataChanged(mi, mi, {MarkRole});
    }
}

QString ImageListModel::markAtSourceIndex(int sourceIndex) const
{
    if (!m_markManager || sourceIndex < 0 || sourceIndex >= m_imagePaths.size()) {
        return QString();
    }

    return m_markManager->markForImageKey(m_normalizedFolderPath, m_markKeys.at(sourceIndex));
}
