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
 * @param ignoreColorProfile When true, the embedded ICC / color-profile tag is
 *        stripped from the loaded image (see stripColorProfile). Pixel data is
 *        not transformed; only the profile metadata is cleared.
 * @return The generated thumbnail as QImage. Returns a null QImage on failure.
 */
QImage generateThumbnail(const QString &imagePath,
                         const QSize &thumbnailSize = QSize(200, 200),
                         Qt::TransformationMode transformMode = Qt::SmoothTransformation,
                         bool ignoreColorProfile = false);

/**
 * @brief Generate a thumbnail from an existing QImage.
 * @param image Source image.
 * @param thumbnailSize Maximum size of the thumbnail (preserves aspect ratio).
 * @param ignoreColorProfile When true, the resulting thumbnail has its color
 *        profile stripped (see stripColorProfile). Has no effect on a null image.
 * @return The generated thumbnail as QImage.
 */
QImage generateThumbnail(const QImage &image,
                         const QSize &thumbnailSize = QSize(200, 200),
                         Qt::TransformationMode transformMode = Qt::SmoothTransformation,
                         bool ignoreColorProfile = false);

/**
 * @brief Strip the embedded color-profile tag from an image in place.
 *
 * Clears QImage::colorSpace() so the image is treated as untagged sRGB-like
 * pixel data — no color-management transform is applied during rendering.
 * Pixel bytes are not modified. No-op on a null image.
 */
void stripColorProfile(QImage &image);

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
