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

    // Use scaled reading for efficiency — avoids loading the full image into memory
    QSize originalSize = reader.size();
    if (originalSize.isValid()) {
        QSize scaled = originalSize.scaled(thumbnailSize, Qt::KeepAspectRatio);
        reader.setScaledSize(scaled);
    }

    QImage image = reader.read();
    if (image.isNull()) {
        return QImage();
    }

    // If the reader didn't support setScaledSize, scale manually
    if (image.size() != image.size().scaled(thumbnailSize, Qt::KeepAspectRatio)) {
        image = image.scaled(thumbnailSize, Qt::KeepAspectRatio, transformMode);
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
    QImage scaled = image.scaled(thumbnailSize, Qt::KeepAspectRatio, transformMode);
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
