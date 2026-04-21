#include "ImageUtils.h"

#include <QImageReader>

namespace ImageUtils
{

QImage generateThumbnail(const QString &imagePath,
                         const QSize &thumbnailSize,
                         Qt::TransformationMode transformMode)
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
        return image.scaled(thumbnailSize, Qt::KeepAspectRatio, transformMode);
    }

    return image;
}

QImage generateThumbnail(const QImage &image,
                         const QSize &thumbnailSize,
                         Qt::TransformationMode transformMode)
{
    if (image.isNull()) {
        return QImage();
    }
    return image.scaled(thumbnailSize, Qt::KeepAspectRatio, transformMode);
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
