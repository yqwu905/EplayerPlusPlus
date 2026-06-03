#include "ImageListModel.h"
#include "services/ImageLoader.h"
#include "services/ImageMarkManager.h"
#include "utils/FileUtils.h"

#include <QDir>
#include <QFileInfo>
#include <QtConcurrent>
#include <QMetaObject>

#include <algorithm>
#include <numeric>
#include <utility>

namespace
{
// Upper bound on distinct decoded thumbnails kept in memory per folder column.
// Far larger than any visible + prefetch window, so on-screen items are never
// evicted; it only caps the long tail accumulated while scrolling huge folders.
constexpr int kThumbnailCacheCap = 800;
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
    clearThumbnailCache();
    m_sourceModifiedUtc.clear();
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
    clearThumbnailCache();
    m_sourceModifiedUtc.clear();
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

void ImageListModel::setThumbnailSize(const QSize &size)
{
    if (!size.isValid() || size == m_thumbnailSize) {
        return;
    }
    // Existing thumbnails stay cached for instant display; growing the bucket lets
    // loadThumbnailsForRange() upgrade visible items to a sharper decode (gated by
    // m_upgradedExtent), shrinking simply keeps the larger image and downscales it.
    m_thumbnailSize = size;
}

QSize ImageListModel::thumbnailSize() const
{
    return m_thumbnailSize;
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

    const int bucket = m_thumbnailSize.width();
    QStringList visibleFirst;
    visibleFirst.reserve(lastVisible - firstVisible + 1);
    for (int i = firstVisible; i <= lastVisible; ++i) {
        const QString path = imagePathAt(i);
        if (path.isEmpty()) {
            continue;
        }

        auto it = m_thumbnails.constFind(path);
        bool needRequest = false;
        if (it == m_thumbnails.constEnd()) {
            // Never loaded (or evicted): request unconditionally. Robust to a
            // prior cancellation — re-requested until it actually arrives.
            needRequest = true;
        } else {
            touchThumbnail(path);
            // Zoomed in past the cached resolution: upgrade once per bucket.
            if (qMax(it->width(), it->height()) < bucket &&
                m_upgradedExtent.value(path, 0) < bucket) {
                needRequest = true;
            }
        }

        if (needRequest) {
            m_upgradedExtent.insert(path, bucket);
            visibleFirst.append(path);
        }
    }

    if (!visibleFirst.isEmpty()) {
        m_imageLoader->requestThumbnailBatchVisibleFirst(visibleFirst, m_thumbnailSize,
                                                         m_sourceModifiedUtc);
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
        for (const QString &path : pathsToLoad) {
            m_upgradedExtent.insert(path, m_thumbnailSize.width());
        }
        m_imageLoader->requestThumbnailBatch(pathsToLoad, m_thumbnailSize,
                                             m_sourceModifiedUtc);
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
    // thumbnailReady is a shared signal; ignore anything larger than this column's
    // current decode bucket (e.g. a stale, sharper request that arrives after the
    // user has zoomed back out — the already-cached image downscales fine).
    if (thumbnail.width() > m_thumbnailSize.width() ||
        thumbnail.height() > m_thumbnailSize.height()) {
        return;
    }

    auto it = m_pathToIndex.constFind(imagePath);
    if (it == m_pathToIndex.constEnd()) {
        return;
    }
    const int sourceIndex = it.value();

    storeThumbnail(imagePath, thumbnail);

    const int row = rowForSourceIndex(sourceIndex);
    if (row < 0) {
        return;
    }

    QModelIndex mi = this->index(row);
    emit dataChanged(mi, mi, {Qt::DecorationRole, ThumbnailRole});
}

void ImageListModel::touchThumbnail(const QString &path)
{
    auto posIt = m_thumbnailLruPos.find(path);
    if (posIt == m_thumbnailLruPos.end()) {
        return;
    }
    // Move the node to the front (MRU). splice keeps the iterator valid.
    m_thumbnailLru.splice(m_thumbnailLru.begin(), m_thumbnailLru, posIt.value());
}

void ImageListModel::storeThumbnail(const QString &path, const QImage &thumbnail)
{
    auto posIt = m_thumbnailLruPos.find(path);
    if (posIt != m_thumbnailLruPos.end()) {
        m_thumbnails.insert(path, thumbnail);
        touchThumbnail(path);
        return;
    }

    m_thumbnailLru.push_front(path);
    m_thumbnailLruPos.insert(path, m_thumbnailLru.begin());
    m_thumbnails.insert(path, thumbnail);
    trimThumbnailCache();
}

void ImageListModel::trimThumbnailCache()
{
    while (m_thumbnails.size() > kThumbnailCacheCap && !m_thumbnailLru.empty()) {
        const QString victim = m_thumbnailLru.back();
        m_thumbnailLru.pop_back();
        m_thumbnailLruPos.remove(victim);
        m_thumbnails.remove(victim);
        m_upgradedExtent.remove(victim);
    }
}

void ImageListModel::clearThumbnailCache()
{
    m_thumbnails.clear();
    m_thumbnailLru.clear();
    m_thumbnailLruPos.clear();
    m_upgradedExtent.clear();
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
    m_scanFuture = QtConcurrent::run([this, path, generation, cancelToken]() {
        FileUtils::ScanOptions options;
        options.recursive = false;
        options.batchSize = 64;
        options.initialBatchSize = 24;

        FileUtils::scanForImagesBatched(
            path,
            options,
            [this, generation](const QVector<FileUtils::ScannedImage> &batch, bool /*initialBatch*/) {
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

void ImageListModel::appendScanBatch(const QVector<FileUtils::ScannedImage> &batch, int generation)
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
    m_sourceModifiedUtc.reserve(m_sourceModifiedUtc.size() + batch.size());

    if (!hasActiveFilters()) {
        const int beginRow = m_imagePaths.size();
        const int endRow = beginRow + batch.size() - 1;
        beginInsertRows(QModelIndex(), beginRow, endRow);
        for (const FileUtils::ScannedImage &item : batch) {
            const QString &path = item.path;
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
            m_sourceModifiedUtc.insert(path, item.lastModifiedUtc);
        }
        endInsertRows();
    } else {
        for (const FileUtils::ScannedImage &item : batch) {
            const QString &path = item.path;
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
            m_sourceModifiedUtc.insert(path, item.lastModifiedUtc);

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
            candidates.reserve(batch.size());
            for (const FileUtils::ScannedImage &item : batch) {
                candidates.append(item.path);
            }
        } else {
            for (int i = qMax(0, imageCount() - batch.size()); i < imageCount(); ++i) {
                const QString path = imagePathAt(i);
                if (!path.isEmpty()) {
                    candidates.append(path);
                }
            }
        }

        const int count = qMin(m_initialPrefetchRemaining, candidates.size());
        const QStringList initial = candidates.mid(0, count);
        for (const QString &path : initial) {
            m_upgradedExtent.insert(path, m_thumbnailSize.width());
        }
        m_imageLoader->requestThumbnailBatchVisibleFirst(initial,
                                                         m_thumbnailSize,
                                                         m_sourceModifiedUtc);
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

    // The scan delivered images in discovery order for fast first paint; now that
    // every file is known, snap the list into its final globally sorted order.
    sortSourcesByPath();

    m_loading = false;
    m_nextLoadIndex = 0;
    emit folderReady();
}

void ImageListModel::sortSourcesByPath()
{
    const int n = m_imagePaths.size();
    if (n <= 1) {
        return;
    }

    // order[newIndex] = old source index that belongs at newIndex once sorted.
    QVector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [this](int a, int b) {
        return m_imagePaths.at(a) < m_imagePaths.at(b);
    });

    // Skip the reset (and the view churn it causes) when discovery order already
    // happened to be sorted.
    if (std::is_sorted(order.cbegin(), order.cend())) {
        return;
    }

    // Selection and thumbnails are keyed by path, which is stable across the
    // reorder; snapshot the selected paths so we can restore the set afterwards.
    QSet<QString> selectedPaths;
    selectedPaths.reserve(m_selectedIndices.size());
    for (int oldIndex : std::as_const(m_selectedIndices)) {
        if (oldIndex >= 0 && oldIndex < n) {
            selectedPaths.insert(m_imagePaths.at(oldIndex));
        }
    }

    QStringList sortedPaths;
    QStringList sortedFileNames;
    QStringList sortedMarkKeys;
    sortedPaths.reserve(n);
    sortedFileNames.reserve(n);
    sortedMarkKeys.reserve(n);
    for (int i = 0; i < n; ++i) {
        const int oldIndex = order.at(i);
        sortedPaths.append(m_imagePaths.at(oldIndex));
        sortedFileNames.append(m_fileNames.at(oldIndex));
        sortedMarkKeys.append(m_markKeys.at(oldIndex));
    }

    beginResetModel();

    m_imagePaths = std::move(sortedPaths);
    m_fileNames = std::move(sortedFileNames);
    m_markKeys = std::move(sortedMarkKeys);

    // Rebuild the value->index maps, mirroring appendScanBatch's collision rules:
    // path is unique; markKey (when non-empty) takes the last index; fileName keeps
    // the first index seen in sorted order.
    m_pathToIndex.clear();
    m_pathToIndex.reserve(n);
    m_markKeyToIndex.clear();
    m_markKeyToIndex.reserve(n);
    m_fileNameToIndex.clear();
    m_fileNameToIndex.reserve(n);
    for (int i = 0; i < n; ++i) {
        m_pathToIndex.insert(m_imagePaths.at(i), i);
        const QString &markKey = m_markKeys.at(i);
        if (!markKey.isEmpty()) {
            m_markKeyToIndex.insert(markKey, i);
        }
        const QString &fileName = m_fileNames.at(i);
        if (!m_fileNameToIndex.contains(fileName)) {
            m_fileNameToIndex.insert(fileName, i);
        }
    }

    m_selectedIndices.clear();
    if (!selectedPaths.isEmpty()) {
        for (int i = 0; i < n; ++i) {
            if (selectedPaths.contains(m_imagePaths.at(i))) {
                m_selectedIndices.insert(i);
            }
        }
    }

    rebuildFilteredRows();
    m_nextLoadIndex = 0;

    endResetModel();
}

void ImageListModel::cancelPendingScan()
{
    if (m_scanCancelToken) {
        m_scanCancelToken->cancel();
        m_scanCancelToken.reset();
    }

    // Cancelling the token only asks the background scan to stop; we must also
    // block until the worker has actually returned. The scan thread captures
    // `this` and posts batches back via QMetaObject::invokeMethod(this, ...), so a
    // scan still in flight when the model (or the QApplication) is destroyed
    // dereferences freed memory — a teardown use-after-free that segfaulted
    // tst_BrowsePanel on Windows CI. scanForImagesBatched checks the cancel flag
    // once per directory entry, so this wait returns promptly.
    if (m_scanFuture.isRunning()) {
        m_scanFuture.waitForFinished();
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
