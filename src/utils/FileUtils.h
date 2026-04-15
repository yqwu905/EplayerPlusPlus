#ifndef FILEUTILS_H
#define FILEUTILS_H

#include <QStringList>
#include <QDir>

/**
 * @brief File system utility functions for scanning and filtering image files.
 */
namespace FileUtils
{

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

/**
 * @brief Get immediate subdirectories of a given directory.
 * @param dirPath Path to the parent directory.
 * @return List of absolute paths to subdirectories.
 */
QStringList getSubdirectories(const QString &dirPath);

} // namespace FileUtils

#endif // FILEUTILS_H
