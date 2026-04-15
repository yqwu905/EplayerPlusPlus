#include "FileUtils.h"

#include <QFileInfo>
#include <QDirIterator>
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
