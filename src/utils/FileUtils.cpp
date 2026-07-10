#include "FileUtils.h"

#include <QFileInfo>
#include <QDirIterator>
#include <QSet>
#include <algorithm>

namespace
{
QString lowerSuffix(const QString &fileName)
{
    const int dot = fileName.lastIndexOf(QLatin1Char('.'));
    return dot >= 0 ? fileName.mid(dot + 1).toLower() : QString();
}
}

namespace FileUtils
{

bool isImageFile(const QString &filePath, const QStringList &extensions)
{
    QFileInfo fi(filePath);
    if (!fi.isFile()) {
        return false;
    }
    const QString suffix = lowerSuffix(fi.fileName());
    return extensions.contains(suffix);
}

QStringList scanForImages(const QString &dirPath, bool recursive, const QStringList &extensions)
{
    QStringList result;

    QDir dir(dirPath);
    if (!dir.exists()) {
        return result;
    }

    QDirIterator::IteratorFlags flags = QDirIterator::NoIteratorFlags;
    if (recursive) {
        flags = QDirIterator::Subdirectories;
    }

    // Match by lowercased suffix rather than glob name filters: QDir name
    // filters are case-sensitive on case-sensitive filesystems (Linux, some
    // macOS volumes), which would silently skip IMG_0001.JPG / photo.PNG.
    // This mirrors isImageFile() so the scan accepts exactly what it accepts.
    QDirIterator it(dirPath, QDir::Files, flags);
    QSet<QString> extensionSet;
    extensionSet.reserve(extensions.size());
    for (const QString &extension : extensions) {
        extensionSet.insert(extension.toLower());
    }
    while (it.hasNext()) {
        it.next();
        if (!extensionSet.contains(lowerSuffix(it.fileName()))) {
            continue;
        }
        result << it.filePath();
    }

    // Sort for deterministic ordering
    std::sort(result.begin(), result.end());
    return result;
}

void scanForImagesBatched(
    const QString &dirPath,
    const ScanOptions &options,
    const std::function<void(const QVector<ScannedImage> &batch, bool initialBatch)> &onBatch,
    const std::function<void(const ScanProgress &progress)> &onProgress,
    const std::shared_ptr<ScanCancelToken> &cancelToken)
{
    if (!onBatch) {
        return;
    }

    auto cancelled = [&cancelToken]() {
        return cancelToken && cancelToken->isCancelled();
    };
    // A scan canceled while it was still queued should not touch the target at
    // all. On a disconnected network mount even the initial exists() probe can
    // block a worker for a long time.
    if (cancelled()) {
        return;
    }

    QDir root(dirPath);
    if (!root.exists()) {
        if (onProgress) {
            onProgress({0, true});
        }
        return;
    }

    const int batchSize = qBound(1, options.batchSize, 5000);
    const int initialBatchSize = options.initialBatchSize > 0
        ? qMin(options.initialBatchSize, batchSize)
        : 0;
    bool initialBatchSent = (initialBatchSize == 0);
    int discoveredCount = 0;

    auto emitProgress = [&](bool finished) {
        if (onProgress) {
            onProgress({discoveredCount, finished});
        }
    };

    // Files are delivered in directory-iteration (discovery) order so thumbnails
    // can paint as soon as they are found. The order is *not* sorted here:
    // ImageListModel re-sorts the whole list by path once the scan completes
    // (see ImageListModel::finalizeScan). Sorting per batch would be wasted work
    // and could not produce a correct global order across batches anyway.
    QVector<ScannedImage> buffer;
    buffer.reserve(batchSize);
    auto flush = [&](bool initialBatch) {
        if (buffer.isEmpty()) {
            return;
        }
        onBatch(buffer, initialBatch);
        buffer.clear();
        emitProgress(false);
    };

    QDirIterator::IteratorFlags flags = QDirIterator::NoIteratorFlags;
    if (options.recursive) {
        flags = QDirIterator::Subdirectories;
    }

    // Match by lowercased suffix rather than glob name filters: QDir name
    // filters are case-sensitive on case-sensitive filesystems, which would
    // silently skip uppercase/mixed-case extensions (IMG_0001.JPG, scan.TIFF).
    // This mirrors isImageFile() exactly.
    QDirIterator it(dirPath, QDir::Files | QDir::NoSymLinks, flags);
    QSet<QString> extensionSet;
    extensionSet.reserve(options.extensions.size());
    for (const QString &extension : options.extensions) {
        extensionSet.insert(extension.toLower());
    }
    while (true) {
        if (cancelled()) {
            return;
        }

        if (!it.hasNext()) {
            break;
        }
        // Cancellation can land while hasNext() is enumerating a slow share.
        // Check again before advancing/processing the entry.
        if (cancelled()) {
            return;
        }

        const QString path = it.next();
        const QString fileName = it.fileName();
        // QFileInfo constructed from a bare filename only parses the suffix; it
        // does not touch the file system. Avoid QDirIterator::fileInfo() entirely
        // on the fast-list path because lastModified() can otherwise force one
        // stat per entry on APFS and remote shares.
        if (!extensionSet.contains(lowerSuffix(fileName))) {
            continue;
        }
        const QDateTime modifiedUtc = options.captureLastModified
            ? QFileInfo(path).lastModified().toUTC()
            : QDateTime();
        buffer.push_back({path, fileName, modifiedUtc});
        ++discoveredCount;
        if (!initialBatchSent && buffer.size() >= initialBatchSize) {
            flush(true);
            initialBatchSent = true;
        } else if (buffer.size() >= batchSize) {
            flush(false);
        }
    }

    if (cancelled()) {
        return;
    }
    if (!buffer.isEmpty()) {
        flush(!initialBatchSent);
        initialBatchSent = true;
    }
    emitProgress(true);
}

QStringList getSubdirectories(const QString &dirPath)
{
    QStringList result;

    QDir dir(dirPath);
    if (!dir.exists()) {
        return result;
    }

    const QFileInfoList entries = dir.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

    for (const QFileInfo &fi : entries) {
        result << fi.absoluteFilePath();
    }

    return result;
}

} // namespace FileUtils
