#include "ImageUtils.h"

#include <QColorSpace>
#include <QImageReader>

namespace ImageUtils
{

QImage generateThumbnail(const QString &imagePath,
                         const QSize &thumbnailSize,
                         Qt::TransformationMode transformMode,
                         bool ignoreColorProfile)
{
    QImageReader reader(imagePath);
    reader.setAutoTransform(true);

    // Use scaled reading for efficiency — avoids loading the full image into memory.
    // Only ever downscale: a source smaller than the thumbnail box should stay at
    // native size, not be blurrily enlarged into a bigger buffer. boundedTo caps
    // the fitted size at the source; KeepAspectRatio scales both dimensions by one
    // factor, so the clamp is uniform and preserves aspect ratio.
    QSize originalSize = reader.size();
    if (originalSize.isValid()) {
        const QSize scaled = originalSize.scaled(thumbnailSize, Qt::KeepAspectRatio)
                                 .boundedTo(originalSize);
        reader.setScaledSize(scaled);
    }

    QImage image = reader.read();
    if (image.isNull()) {
        return QImage();
    }

    // If the reader didn't honor setScaledSize, scale manually — but only down.
    const QSize fit = image.size().scaled(thumbnailSize, Qt::KeepAspectRatio)
                          .boundedTo(image.size());
    if (image.size() != fit) {
        image = image.scaled(fit, Qt::KeepAspectRatio, transformMode);
    }

    if (ignoreColorProfile) {
        stripColorProfile(image);
    }
    return image;
}

QImage generateThumbnail(const QImage &image,
                         const QSize &thumbnailSize,
                         Qt::TransformationMode transformMode,
                         bool ignoreColorProfile)
{
    if (image.isNull()) {
        return QImage();
    }
    // Clamp the target box to the source so small images are returned at native
    // size instead of being upscaled (blurry + wasted memory). KeepAspectRatio
    // makes this a no-op for the normal downscale path.
    QImage scaled = image.scaled(thumbnailSize.boundedTo(image.size()),
                                 Qt::KeepAspectRatio, transformMode);
    if (ignoreColorProfile) {
        stripColorProfile(scaled);
    }
    return scaled;
}

void stripColorProfile(QImage &image)
{
    if (image.isNull()) {
        return;
    }
    // Assigning a default-constructed (invalid) QColorSpace clears any embedded
    // ICC tag. QImage::setColorSpace does not touch pixel data when the new
    // colorSpace is invalid — it simply drops the tag.
    image.setColorSpace(QColorSpace());
}

QImage scaleImage(const QImage &image, const QSize &targetSize)
{
    if (image.isNull() || !targetSize.isValid()) {
        return QImage();
    }
    return image.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

bool isValidImage(const QString &filePath)
{
    QImageReader reader(filePath);
    return reader.canRead();
}

QSize getImageSize(const QString &filePath)
{
    QImageReader reader(filePath);
    return reader.size();
}

} // namespace ImageUtils
