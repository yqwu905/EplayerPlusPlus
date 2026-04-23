#ifndef FILEUTILS_H
#define FILEUTILS_H

#include <QStringList>
#include <QDir>
#include <functional>
#include <memory>
#include <atomic>

/**
 * @brief File system utility functions for scanning and filtering image files.
 */
namespace FileUtils
{

class ScanCancelToken
{
public:
    void cancel() { m_cancelled.store(true, std::memory_order_relaxed); }
    bool isCancelled() const { return m_cancelled.load(std::memory_order_relaxed); }

private:
    std::atomic<bool> m_cancelled{false};
};

struct ScanProgress {
    int discoveredCount = 0;
    bool finished = false;
};

/**
 * @brief Default list of supported image file extensions.
 */
inline QStringList supportedImageExtensions()
{
    return {
        "png", "jpg", "jpeg", "bmp", "tiff", "tif",
        "gif", "webp", "ico", "svg", "pbm", "pgm", "ppm"
    };
}

struct ScanOptions {
    bool recursive = true;
    int batchSize = 1000;
    int initialBatchSize = 300;
    QStringList extensions = supportedImageExtensions();
};

/**
 * @brief Check if a file path points to a supported image file.
 * @param filePath Path to the file.
 * @param extensions List of allowed extensions (lowercase, without dot).
 * @return true if the file extension matches a supported image format.
 */
bool isImageFile(const QString &filePath,
                 const QStringList &extensions = supportedImageExtensions());

/**
 * @brief Scan a directory for image files.
 * @param dirPath Path to the directory to scan.
 * @param recursive If true, scan subdirectories recursively.
 * @param extensions List of allowed extensions (lowercase, without dot).
 * @return Sorted list of absolute paths to image files found.
 */
QStringList scanForImages(const QString &dirPath,
                          bool recursive = true,
                          const QStringList &extensions = supportedImageExtensions());

void scanForImagesBatched(
    const QString &dirPath,
    const ScanOptions &options,
    const std::function<void(const QStringList &batch, bool initialBatch)> &onBatch,
    const std::function<void(const ScanProgress &progress)> &onProgress = {},
    const std::shared_ptr<ScanCancelToken> &cancelToken = {});

/**
 * @brief Get immediate subdirectories of a given directory.
 * @param dirPath Path to the parent directory.
 * @return List of absolute paths to subdirectories.
 */
QStringList getSubdirectories(const QString &dirPath);

} // namespace FileUtils

#endif // FILEUTILS_H
