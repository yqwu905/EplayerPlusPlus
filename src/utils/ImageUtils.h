#ifndef IMAGEUTILS_H
#define IMAGEUTILS_H

#include <QImage>
#include <QPixmap>
#include <QString>
#include <QSize>

/**
 * @brief Image utility functions for thumbnail generation, scaling, and format detection.
 */
namespace ImageUtils
{

/**
 * @brief Generate a thumbnail from an image file.
 * @param imagePath Path to the source image.
 * @param thumbnailSize Maximum size of the thumbnail (preserves aspect ratio).
 * @return The generated thumbnail as QImage. Returns a null QImage on failure.
 */
QImage generateThumbnail(const QString &imagePath,
                         const QSize &thumbnailSize = QSize(200, 200),
                         Qt::TransformationMode transformMode = Qt::SmoothTransformation);

/**
 * @brief Generate a thumbnail from an existing QImage.
 * @param image Source image.
 * @param thumbnailSize Maximum size of the thumbnail (preserves aspect ratio).
 * @return The generated thumbnail as QImage.
 */
QImage generateThumbnail(const QImage &image,
                         const QSize &thumbnailSize = QSize(200, 200),
                         Qt::TransformationMode transformMode = Qt::SmoothTransformation);

/**
 * @brief Scale an image to fit within the given size, preserving aspect ratio.
 * @param image Source image.
 * @param targetSize Maximum bounding size.
 * @return Scaled image.
 */
QImage scaleImage(const QImage &image, const QSize &targetSize);

/**
 * @brief Check if a file is a valid, loadable image.
 * @param filePath Path to the file.
 * @return true if the file can be loaded as a QImage.
 */
bool isValidImage(const QString &filePath);

/**
 * @brief Get image dimensions without fully loading the image.
 * @param filePath Path to the image file.
 * @return Image size, or invalid QSize if the file cannot be read.
 */
QSize getImageSize(const QString &filePath);

} // namespace ImageUtils

#endif // IMAGEUTILS_H
