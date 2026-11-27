#ifndef IMAGEINFORMATION_H
#define IMAGEINFORMATION_H

#include <QImage>
#include <QStringList>

#include <array>

struct ImageInformation
{
    QStringList exifLines;
    std::array<std::array<quint64, 256>, 3> histogram{};
};

ImageInformation readImageInformation(const QString &path, const QImage &image);

#endif
