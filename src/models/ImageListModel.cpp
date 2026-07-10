#include "ImageListModel.h"
#include "services/ImageLoader.h"
#include "services/ImageMarkManager.h"
#include "utils/FileUtils.h"

#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QPromise>
#include <QtConcurrent>

#include <algorithm>
#include <numeric>
#include <utility>

namespace
{
// Upper bound on distinct decoded thumbnails kept in memory per folder column.
// Far larger than any visible + prefetch window, so on-screen items are never
// evicted; it only caps the long tail accumulated while scrolling huge folders.
constexpr int kThumbnailCacheCap = 256;
constexpr qint64 kThumbnailCacheByteCap = 32LL * 1024LL * 1024LL;
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
    case MarkSourceRole:
        return markSourceAtSourceIndex(sourceIndex);
    case MarkReasonRole:
        return markReasonAtSourceIndex(sourceIndex);
    case Qt::ToolTipRole: {
        const CachedMark &mark = m_marks.at(sourceIndex);
        if (mark.source == ImageMarkManager::vlmSource() && !mark.reason.isEmpty()) {
            return mark.reason;
        }
        return QVariant();
    }
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
    m_marks.clear();
    m_markSnapshot.clear();
    m_fileNames.clear();
    m_fileNameToIndex.clear();
    m_filteredSourceRows.clear();
    m_selectedIndices.clear();
    clearThumbnailCache();
    m_sourceModifiedUtc.clear();
    m_nextLoadIndex = 0;
    endResetModel();

    if (m_markManager) {
        m_markManager->loadFolderAsync(m_normalizedFolderPath);
    }
    refreshMarkSnapshot();

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
    m_marks.clear();
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

    refreshMarkSnapshot();
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
    setFilters(filterText, m_categoryFilter, m_imagePathFilter,
               m_imagePathFilterEnabled);
}

QString ImageListModel::fileNameFilter() const
{
    return m_fileNameFilter;
}

void ImageListModel::setCategoryFilter(const QString &category)
{
    setFilters(m_fileNameFilter, category, m_imagePathFilter,
               m_imagePathFilterEnabled);
}

QString ImageListModel::categoryFilter() const
{
    return m_categoryFilter;
}

void ImageListModel::setFilters(const QString &fileNameFilter,
                                const QString &categoryFilter,
                                const QSet<QString> &imagePaths,
                                bool imagePathFilterEnabled)
{
    const QString normalizedFileName = fileNameFilter.trimmed();
    const QString normalizedCategory = categoryFilter.trimmed();
    if (!normalizedCategory.isEmpty() &&
        normalizedCategory != kUnmarkedCategoryFilter &&
        !ImageMarkManager::isValidCategory(normalizedCategory)) {
        return;
    }

    const QSet<QString> normalizedPaths = imagePathFilterEnabled
        ? imagePaths
        : QSet<QString>{};
    if (m_fileNameFilter == normalizedFileName &&
        m_categoryFilter == normalizedCategory &&
        m_imagePathFilterEnabled == imagePathFilterEnabled &&
        m_imagePathFilter == normalizedPaths) {
        return;
    }

    m_fileNameFilter = normalizedFileName;
    m_categoryFilter = normalizedCategory;
    m_imagePathFilterEnabled = imagePathFilterEnabled;
    m_imagePathFilter = normalizedPaths;
    applyFilters();
}

void ImageListModel::setImagePathFilter(const QSet<QString> &imagePaths, bool enabled)
{
    setFilters(m_fileNameFilter, m_categoryFilter, imagePaths, enabled);
}

void ImageListModel::clearImagePathFilter()
{
    setImagePathFilter({}, false);
}

bool ImageListModel::hasImagePathFilter() const
{
    return m_imagePathFilterEnabled;
}

bool ImageListModel::hasActiveFilters() const
{
    return !m_fileNameFilter.isEmpty() ||
        !m_categoryFilter.isEmpty() ||
        m_imagePathFilterEnabled;
}

int ImageListModel::indexOfFileName(const QString &fileName) const
{
    if (hasActiveFilters()) {
        const int indexedSource = m_fileNameToIndex.value(fileName, -1);
        const int indexedRow = rowForSourceIndex(indexedSource);
        if (indexedRow >= 0) {
            return indexedRow;
        }
        // Duplicate filenames can exist in future recursive scans. If the first
        // indexed occurrence is filtered out, retain the legacy fallback that
        // finds another visible occurrence.
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

int ImageListModel::sourceIndexOfFileName(const QString &fileName) const
{
    return m_fileNameToIndex.value(fileName, -1);
}

int ImageListModel::indexOfImagePath(const QString &imagePath) const
{
    const int sourceIndex = m_pathToIndex.value(imagePath, -1);
    return rowForSourceIndex(sourceIndex);
}

int ImageListModel::sourceRowForRow(int row) const
{
    return sourceIndexForRow(row);
}

QString ImageListModel::imagePathAtSourceRow(int sourceRow) const
{
    if (sourceRow < 0 || sourceRow >= m_imagePaths.size()) {
        return QString();
    }
    return m_imagePaths.at(sourceRow);
}

QString ImageListModel::fileNameAtSourceRow(int sourceRow) const
{
    if (sourceRow < 0 || sourceRow >= m_fileNames.size()) {
        return QString();
    }
    return m_fileNames.at(sourceRow);
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
        connect(m_markManager, &ImageMarkManager::folderLoaded,
                this, [this](const QString &folderPath) {
            if (folderPath != m_normalizedFolderPath) {
                return;
            }
            refreshMarkSnapshot();
            if (!m_categoryFilter.isEmpty()) {
                applyFilters();
            } else if (imageCount() > 0) {
                emit dataChanged(index(0), index(imageCount() - 1),
                                 {MarkRole, MarkSourceRole, MarkReasonRole,
                                  Qt::ToolTipRole});
            }
        });
        if (!m_folderPath.isEmpty()) {
            m_markManager->loadFolderAsync(m_normalizedFolderPath);
        }
    }

    refreshMarkSnapshot();

    if (!m_categoryFilter.isEmpty()) {
        applyFilters();
        return;
    }

    if (imageCount() > 0) {
        emit dataChanged(index(0),
                         index(imageCount() - 1),
                         {MarkRole, MarkSourceRole, MarkReasonRole, Qt::ToolTipRole});
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
        connect(m_imageLoader, &ImageLoader::thumbnailReadyDetailed,
                this, &ImageListModel::onThumbnailReady);
    }
}

void ImageListModel::invalidateThumbnailCache()
{
    clearThumbnailCache();
}

void ImageListModel::setThumbnailDeliveryEnabled(bool enabled)
{
    if (m_thumbnailDeliveryEnabled == enabled) {
        return;
    }
    m_thumbnailDeliveryEnabled = enabled;
    if (!enabled) {
        clearThumbnailCache();
    }
}

void ImageListModel::setThumbnailSize(const QSize &size)
{
    if (!size.isValid() || size == m_thumbnailSize) {
        return;
    }
    // Existing thumbnails stay cached for instant display; growing the bucket lets
    // loadThumbnailsForRange() upgrade visible items to a sharper decode; shrinking
    // simply keeps the larger successful image and downscales it.
    m_thumbnailSize = size;
}

QSize ImageListModel::thumbnailSize() const
{
    return m_thumbnailSize;
}

void ImageListModel::loadThumbnailsForRange(int firstVisible, int lastVisible,
                                            bool prefetchPriority)
{
    if (!m_thumbnailDeliveryEnabled || !m_imageLoader || imageCount() <= 0) {
        return;
    }

    firstVisible = qMax(0, firstVisible);
    lastVisible = qMin(imageCount() - 1, lastVisible);
    if (lastVisible < firstVisible) {
        return;
    }

    const int bucket = m_thumbnailSize.width();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    QStringList visibleFirst;
    visibleFirst.reserve(lastVisible - firstVisible + 1);
    for (int i = firstVisible; i <= lastVisible; ++i) {
        const QString path = imagePathAt(i);
        if (path.isEmpty()) {
            continue;
        }
        if (m_thumbnailRetryAfterMs.value(path, 0) > nowMs) {
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
            // requested extent records a successful completion, even for a tiny
            // source that cannot physically reach the bucket. Until completion,
            // repeat requests are safe because ImageLoader deduplicates them.
            if (m_thumbnailCompletedExtent.value(path, 0) < bucket) {
                needRequest = true;
            }
        }

        if (needRequest) {
            visibleFirst.append(path);
        }
    }

    if (!visibleFirst.isEmpty()) {
        if (prefetchPriority) {
            m_imageLoader->requestThumbnailBatchPrefetch(visibleFirst, m_thumbnailSize,
                                                         m_sourceModifiedUtc);
        } else {
            m_imageLoader->requestThumbnailBatchVisibleFirst(visibleFirst, m_thumbnailSize,
                                                             m_sourceModifiedUtc);
        }
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

void ImageListModel::onThumbnailReady(const QString &imagePath,
                                      const QImage &thumbnail,
                                      const QSize &requestedSize,
                                      bool highQuality)
{
    if (!m_thumbnailDeliveryEnabled) {
        return;
    }
    // thumbnailReadyDetailed is broadcast by a shared loader. Reject results
    // for other folder models before recording retry/completion state.
    auto pathIt = m_pathToIndex.constFind(imagePath);
    if (pathIt == m_pathToIndex.constEnd()) {
        return;
    }

    const int requestedExtent = qMax(requestedSize.width(), requestedSize.height());
    const int currentExtent = qMax(m_thumbnailSize.width(), m_thumbnailSize.height());
    // A stale size bucket must neither replace current pixels nor suppress a
    // newer request after the user changes thumbnail zoom.
    if (requestedExtent > currentExtent) {
        return;
    }

    if (thumbnail.isNull()) {
        // Corrupt/temporarily unavailable files stay retryable, but UI relayouts
        // and scroll notifications must not hammer the decoder continuously.
        // Only the current bucket owns the backoff; a failed smaller request
        // must not suppress the sharper request now needed by this column.
        if (requestedExtent < currentExtent) {
            return;
        }
        m_thumbnailRetryAfterMs.insert(imagePath,
                                       QDateTime::currentMSecsSinceEpoch() + 2000);
        return;
    }
    m_thumbnailRetryAfterMs.remove(imagePath);

    const int sourceIndex = pathIt.value();

    const int oldExtent = m_thumbnailCompletedExtent.value(imagePath, 0);
    const bool oldHighQuality = m_thumbnailHighQuality.value(imagePath, false);
    if (requestedExtent < oldExtent ||
        (requestedExtent == oldExtent && oldHighQuality && !highQuality)) {
        return;
    }

    storeThumbnail(imagePath, thumbnail, requestedExtent, highQuality);

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

void ImageListModel::storeThumbnail(const QString &path,
                                    const QImage &thumbnail,
                                    int completedExtent,
                                    bool highQuality)
{
    auto posIt = m_thumbnailLruPos.find(path);
    if (posIt != m_thumbnailLruPos.end()) {
        m_thumbnailCacheBytes -= m_thumbnails.value(path).sizeInBytes();
        m_thumbnails.insert(path, thumbnail);
        m_thumbnailCacheBytes += thumbnail.sizeInBytes();
        m_thumbnailCompletedExtent.insert(path, completedExtent);
        m_thumbnailHighQuality.insert(path, highQuality);
        touchThumbnail(path);
        trimThumbnailCache();
        return;
    }

    m_thumbnailLru.push_front(path);
    m_thumbnailLruPos.insert(path, m_thumbnailLru.begin());
    m_thumbnails.insert(path, thumbnail);
    m_thumbnailCompletedExtent.insert(path, completedExtent);
    m_thumbnailHighQuality.insert(path, highQuality);
    m_thumbnailCacheBytes += thumbnail.sizeInBytes();
    trimThumbnailCache();
}

void ImageListModel::trimThumbnailCache()
{
    while ((m_thumbnails.size() > kThumbnailCacheCap ||
            m_thumbnailCacheBytes > kThumbnailCacheByteCap) &&
           !m_thumbnailLru.empty()) {
        const QString victim = m_thumbnailLru.back();
        m_thumbnailLru.pop_back();
        m_thumbnailLruPos.remove(victim);
        m_thumbnailCacheBytes -= m_thumbnails.value(victim).sizeInBytes();
        m_thumbnails.remove(victim);
        m_thumbnailCompletedExtent.remove(victim);
        m_thumbnailHighQuality.remove(victim);
        m_thumbnailRetryAfterMs.remove(victim);
    }
}

void ImageListModel::clearThumbnailCache()
{
    m_thumbnails.clear();
    m_thumbnailLru.clear();
    m_thumbnailLruPos.clear();
    m_thumbnailCompletedExtent.clear();
    m_thumbnailHighQuality.clear();
    m_thumbnailRetryAfterMs.clear();
    m_thumbnailCacheBytes = 0;
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
    const ImageMarkManager::MarkMetadata metadata = m_markManager
        ? m_markManager->markMetadataForImageKey(m_normalizedFolderPath, markKey)
        : ImageMarkManager::MarkMetadata{};
    const CachedMark cached{metadata.category, metadata.source, metadata.reason};

    // The signal can arrive while the asynchronous folder scan is still
    // discovering rows. Keep the folder snapshot authoritative even when this
    // image has not reached m_markKeyToIndex yet; appendScanBatch() will then
    // initialize the later row with the current mark instead of stale metadata.
    if (metadata.category.isEmpty() && metadata.source.isEmpty() && metadata.reason.isEmpty()) {
        m_markSnapshot.remove(markKey);
    } else {
        m_markSnapshot.insert(markKey, cached);
    }

    int idx = m_markKeyToIndex.value(markKey, -1);
    if (idx < 0) {
        idx = m_pathToIndex.value(imagePath, -1);
    }
    if (idx < 0) {
        return;
    }
    if (idx >= m_marks.size()) {
        return;
    }
    m_marks[idx] = cached;
    if (metadata.category.isEmpty() && metadata.source.isEmpty() && metadata.reason.isEmpty()) {
        m_markSnapshot.remove(m_markKeys.at(idx));
    } else {
        m_markSnapshot.insert(m_markKeys.at(idx), m_marks.at(idx));
    }

    updateFilteredRowForSourceIndex(idx);
}

void ImageListModel::refreshMarkSnapshot()
{
    m_markSnapshot.clear();
    if (m_markManager && !m_normalizedFolderPath.isEmpty()) {
        const auto snapshot = m_markManager->markMetadataForFolder(m_normalizedFolderPath);
        m_markSnapshot.reserve(snapshot.size());
        for (auto it = snapshot.constBegin(); it != snapshot.constEnd(); ++it) {
            m_markSnapshot.insert(it.key(),
                                  CachedMark{it->category, it->source, it->reason});
        }
    }

    m_marks.clear();
    m_marks.reserve(m_markKeys.size());
    for (const QString &key : std::as_const(m_markKeys)) {
        m_marks.append(m_markSnapshot.value(key));
    }
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
    QFuture<ScanEvent> future = QtConcurrent::run([path, cancelToken](QPromise<ScanEvent> &promise) {
        FileUtils::ScanOptions options;
        options.recursive = false;
        options.captureLastModified = false;
        options.batchSize = 64;
        options.initialBatchSize = 24;

        FileUtils::scanForImagesBatched(
            path,
            options,
            [&promise, &cancelToken](const QVector<FileUtils::ScannedImage> &batch,
                                     bool /*initialBatch*/) {
                if (batch.isEmpty() || cancelToken->isCancelled()) {
                    return;
                }
                ScanEvent event;
                event.kind = ScanEvent::Kind::Batch;
                event.batch = std::make_shared<QVector<FileUtils::ScannedImage>>(batch);
                promise.addResult(std::move(event));
            },
            [&promise, &cancelToken](const FileUtils::ScanProgress &progress) {
                if (cancelToken->isCancelled()) {
                    return;
                }
                ScanEvent event;
                event.kind = ScanEvent::Kind::Progress;
                event.progress = progress;
                promise.addResult(std::move(event));
            },
            cancelToken);
    });

    auto *watcher = new QFutureWatcher<ScanEvent>(this);
    m_scanWatcher = watcher;
    connect(watcher, &QFutureWatcher<ScanEvent>::resultReadyAt, this,
            [this, watcher, generation, cancelToken](int resultIndex) {
        if (generation != m_scanGeneration || cancelToken->isCancelled()) {
            return;
        }

        ScanEvent event = watcher->resultAt(resultIndex);
        if (event.kind == ScanEvent::Kind::Batch) {
            if (event.batch) {
                // Empty the shared holder retained by QFuture as soon as this
                // event is consumed; only the model's compact arrays remain.
                QVector<FileUtils::ScannedImage> batch = std::move(*event.batch);
                appendScanBatch(batch, generation);
            }
            return;
        }

        emit scanProgressChanged(event.progress.discoveredCount,
                                 event.progress.finished);
        if (event.progress.finished) {
            finalizeScan(generation);
        }
    });
    connect(watcher, &QFutureWatcher<ScanEvent>::finished, this,
            [this, watcher, generation, cancelToken]() {
        if (m_scanWatcher == watcher) {
            m_scanWatcher = nullptr;
        }
        if (generation == m_scanGeneration &&
            m_scanCancelToken == cancelToken) {
            m_scanCancelToken.reset();
        }
        watcher->deleteLater();
    });
    watcher->setFuture(future);
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
    m_marks.reserve(m_marks.size() + batch.size());
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
            const QString &fileName = item.fileName;
            // The list scan is intentionally non-recursive, so the normalized
            // folder-relative mark key is exactly the filename. Avoid repeated
            // path cleaning/QDir construction for every discovered row.
            const QString &markKey = fileName;
            m_pathToIndex.insert(path, index);
            m_markKeys.append(markKey);
            m_marks.append(m_markSnapshot.value(markKey));
            if (!markKey.isEmpty()) {
                m_markKeyToIndex.insert(markKey, index);
            }
            if (!m_fileNameToIndex.contains(fileName)) {
                m_fileNameToIndex.insert(fileName, index);
            }
            m_imagePaths.append(path);
            m_fileNames.append(fileName);
            if (item.lastModifiedUtc.isValid()) {
                m_sourceModifiedUtc.insert(path, item.lastModifiedUtc);
            }
        }
        endInsertRows();
    } else {
        QVector<int> matchingSourceRows;
        matchingSourceRows.reserve(batch.size());
        for (const FileUtils::ScannedImage &item : batch) {
            const QString &path = item.path;
            const int sourceIndex = m_imagePaths.size();
            const QString &fileName = item.fileName;
            const QString &markKey = fileName;
            m_pathToIndex.insert(path, sourceIndex);
            m_markKeys.append(markKey);
            m_marks.append(m_markSnapshot.value(markKey));
            if (!markKey.isEmpty()) {
                m_markKeyToIndex.insert(markKey, sourceIndex);
            }
            if (!m_fileNameToIndex.contains(fileName)) {
                m_fileNameToIndex.insert(fileName, sourceIndex);
            }
            m_imagePaths.append(path);
            m_fileNames.append(fileName);
            if (item.lastModifiedUtc.isValid()) {
                m_sourceModifiedUtc.insert(path, item.lastModifiedUtc);
            }

            if (sourceImageMatchesFilters(sourceIndex)) {
                matchingSourceRows.append(sourceIndex);
            }
        }
        if (!matchingSourceRows.isEmpty()) {
            const int firstRow = m_filteredSourceRows.size();
            beginInsertRows(QModelIndex(), firstRow,
                            firstRow + matchingSourceRows.size() - 1);
            m_filteredSourceRows.append(matchingSourceRows);
            endInsertRows();
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
        // This model scans one directory non-recursively, so every absolute path
        // shares the same long prefix. Filename ordering is identical and avoids
        // re-comparing that prefix O(N log N) times.
        return m_fileNames.at(a) < m_fileNames.at(b);
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
    QVector<CachedMark> sortedMarks;
    sortedPaths.reserve(n);
    sortedFileNames.reserve(n);
    sortedMarkKeys.reserve(n);
    sortedMarks.reserve(n);
    for (int i = 0; i < n; ++i) {
        const int oldIndex = order.at(i);
        sortedPaths.append(m_imagePaths.at(oldIndex));
        sortedFileNames.append(m_fileNames.at(oldIndex));
        sortedMarkKeys.append(m_markKeys.at(oldIndex));
        sortedMarks.append(m_marks.at(oldIndex));
    }

    beginResetModel();

    m_imagePaths = std::move(sortedPaths);
    m_fileNames = std::move(sortedFileNames);
    m_markKeys = std::move(sortedMarkKeys);
    m_marks = std::move(sortedMarks);

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
    // Invalidate any already-posted watcher result before changing model state.
    // The watcher is disconnected rather than joined: its worker owns only
    // value data, a promise and the shared cancellation token, so it may safely
    // finish after this model has switched folders or been destroyed.
    ++m_scanGeneration;
    if (m_scanCancelToken) {
        m_scanCancelToken->cancel();
        m_scanCancelToken.reset();
    }

    if (m_scanWatcher) {
        QFutureWatcher<ScanEvent> *watcher = m_scanWatcher;
        m_scanWatcher = nullptr;
        disconnect(watcher, nullptr, this, nullptr);
        // Mark the future canceled as well as setting the cooperative token, so
        // a worker racing between its token check and addResult() cannot retain
        // an undeliverable batch in the future's result store.
        watcher->cancel();
        watcher->deleteLater();
    }

    m_loading = false;
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

    // m_filteredSourceRows is maintained strictly ascending and unique (see
    // rebuildFilteredRows / the lower_bound insert in updateFilteredRowForSourceIndex),
    // so a binary search returns the identical row that the old linear indexOf
    // did. This matters because onThumbnailReady() calls this once per decoded
    // thumbnail; with a filter active, the linear scan made thumbnail delivery
    // O(n^2) over a large filtered folder during scroll.
    const auto it = std::lower_bound(m_filteredSourceRows.cbegin(),
                                     m_filteredSourceRows.cend(), sourceIndex);
    return (it != m_filteredSourceRows.cend() && *it == sourceIndex)
        ? static_cast<int>(std::distance(m_filteredSourceRows.cbegin(), it))
        : -1;
}

bool ImageListModel::sourceImageMatchesFilters(int sourceIndex) const
{
    if (sourceIndex < 0 || sourceIndex >= m_imagePaths.size()) {
        return false;
    }

    if (m_imagePathFilterEnabled && !m_imagePathFilter.contains(m_imagePaths.at(sourceIndex))) {
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
        emit dataChanged(mi, mi, {MarkRole, MarkSourceRole, MarkReasonRole, Qt::ToolTipRole});
        return;
    }

    // Binary search the ascending, unique filtered-row list (same result as the
    // old linear indexOf).
    const auto oldIt = std::lower_bound(m_filteredSourceRows.cbegin(),
                                        m_filteredSourceRows.cend(), sourceIndex);
    const int oldRow = (oldIt != m_filteredSourceRows.cend() && *oldIt == sourceIndex)
        ? static_cast<int>(std::distance(m_filteredSourceRows.cbegin(), oldIt))
        : -1;
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
        emit dataChanged(mi, mi, {MarkRole, MarkSourceRole, MarkReasonRole, Qt::ToolTipRole});
    }
}

QString ImageListModel::markAtSourceIndex(int sourceIndex) const
{
    if (sourceIndex < 0 || sourceIndex >= m_marks.size()) {
        return QString();
    }
    return m_marks.at(sourceIndex).category;
}

QString ImageListModel::markSourceAtSourceIndex(int sourceIndex) const
{
    if (sourceIndex < 0 || sourceIndex >= m_marks.size()) {
        return QString();
    }
    return m_marks.at(sourceIndex).source;
}

QString ImageListModel::markReasonAtSourceIndex(int sourceIndex) const
{
    if (sourceIndex < 0 || sourceIndex >= m_marks.size()) {
        return QString();
    }
    return m_marks.at(sourceIndex).reason;
}
