#include "FileUtils.h"

#include <QFileInfo>
#include <QDirIterator>
#include <QQueue>
#include <algorithm>

namespace FileUtils
{

bool isImageFile(const QString &filePath, const QStringList &extensions)
{
    QFileInfo fi(filePath);
    if (!fi.isFile()) {
        return false;
    }
    const QString suffix = fi.suffix().toLower();
    return extensions.contains(suffix);
}

QStringList scanForImages(const QString &dirPath, bool recursive, const QStringList &extensions)
{
    QStringList result;

    QDir dir(dirPath);
    if (!dir.exists()) {
        return result;
    }

    // Build name filters from extensions (e.g., "*.png", "*.jpg")
    QStringList nameFilters;
    for (const QString &ext : extensions) {
        nameFilters << QStringLiteral("*.%1").arg(ext);
    }

    QDirIterator::IteratorFlags flags = QDirIterator::NoIteratorFlags;
    if (recursive) {
        flags = QDirIterator::Subdirectories;
    }

    QDirIterator it(dirPath, nameFilters, QDir::Files, flags);
    while (it.hasNext()) {
        it.next();
        result << it.filePath();
    }

    // Sort for deterministic ordering
    std::sort(result.begin(), result.end());
    return result;
}

void scanForImagesBatched(
    const QString &dirPath,
    const ScanOptions &options,
    const std::function<void(const QStringList &batch, bool initialBatch)> &onBatch,
    const std::function<void(const ScanProgress &progress)> &onProgress,
    const std::shared_ptr<ScanCancelToken> &cancelToken)
{
    if (!onBatch) {
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
    int initialRemaining = qMax(0, options.initialBatchSize);
    bool initialBatchFlushed = (initialRemaining == 0);
    int discoveredCount = 0;

    auto cancelled = [&cancelToken]() {
        return cancelToken && cancelToken->isCancelled();
    };

    auto emitProgress = [&](bool finished) {
        if (onProgress) {
            onProgress({discoveredCount, finished});
        }
    };

    QStringList buffer;
    buffer.reserve(batchSize);
    auto flush = [&](bool forceInitial = false) {
        if (buffer.isEmpty()) {
            return;
        }

        bool initialBatch = forceInitial;
        if (!initialBatch && initialRemaining > 0) {
            initialBatch = true;
        }
        onBatch(buffer, initialBatch);
        buffer.clear();
        emitProgress(false);
    };

    auto processDir = [&](const QString &folderPath) {
        QDir folder(folderPath);
        QFileInfoList files = folder.entryInfoList(
            QDir::Files | QDir::NoSymLinks, QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo &fi : files) {
            if (cancelled()) {
                return false;
            }
            if (!options.extensions.contains(fi.suffix().toLower())) {
                continue;
            }

            buffer.push_back(fi.absoluteFilePath());
            ++discoveredCount;
            if (initialRemaining > 0) {
                --initialRemaining;
            }
            if (!initialBatchFlushed && initialRemaining == 0 && !buffer.isEmpty()) {
                flush(true);
                initialBatchFlushed = true;
                continue;
            }
            if (buffer.size() >= batchSize) {
                flush();
            }
        }
        return true;
    };

    if (!processDir(dirPath) || cancelled()) {
        return;
    }

    if (options.recursive) {
        QQueue<QString> pendingDirs;
        pendingDirs.enqueue(dirPath);

        while (!pendingDirs.isEmpty()) {
            if (cancelled()) {
                return;
            }

            const QString current = pendingDirs.dequeue();
            QDir dir(current);
            const QFileInfoList subDirs = dir.entryInfoList(
                QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                QDir::Name | QDir::IgnoreCase);

            for (const QFileInfo &subDir : subDirs) {
                if (cancelled()) {
                    return;
                }
                const QString absoluteSubDir = subDir.absoluteFilePath();
                pendingDirs.enqueue(absoluteSubDir);
                if (!processDir(absoluteSubDir)) {
                    return;
                }
            }
        }
    }

    flush();
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
