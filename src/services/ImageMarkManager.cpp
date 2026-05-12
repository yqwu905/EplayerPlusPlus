#include "ImageMarkManager.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace
{
const QString kMarkFileName = QStringLiteral(".imagecompare_marks.json");
const QString kMarkJournalFileName = QStringLiteral(".imagecompare_marks.journal");
const QString kVersionKey = QStringLiteral("version");
const QString kMarksKey = QStringLiteral("marks");
const QString kPathKey = QStringLiteral("path");
const QString kCategoryKey = QStringLiteral("category");
}

ImageMarkManager::ImageMarkManager(QObject *parent)
    : QObject(parent)
{
}

ImageMarkManager::~ImageMarkManager() = default;

QStringList ImageMarkManager::categories()
{
    return {
        QStringLiteral("A"),
        QStringLiteral("B"),
        QStringLiteral("C"),
        QStringLiteral("D")
    };
}

bool ImageMarkManager::isValidCategory(const QString &category)
{
    return categories().contains(category);
}

QString ImageMarkManager::markFilePath(const QString &folderPath) const
{
    return QDir(normalizeFolderPath(folderPath)).filePath(kMarkFileName);
}

QString ImageMarkManager::markJournalPath(const QString &folderPath) const
{
    return QDir(normalizeFolderPath(folderPath)).filePath(kMarkJournalFileName);
}

bool ImageMarkManager::loadFolder(const QString &folderPath)
{
    const QString normalizedFolder = normalizeFolderPath(folderPath);
    if (normalizedFolder.isEmpty()) {
        return false;
    }

    FolderMarks &folderMarks = m_folderMarks[normalizedFolder];
    if (folderMarks.loaded) {
        return true;
    }

    folderMarks.loaded = true;
    folderMarks.marks.clear();

    return loadSnapshot(normalizedFolder, folderMarks) &&
           loadJournal(normalizedFolder, folderMarks);
}

QString ImageMarkManager::markForImage(const QString &folderPath,
                                       const QString &imagePath) const
{
    const QString normalizedFolder = normalizeFolderPath(folderPath);
    const QString key = imageKeyForPath(normalizedFolder, imagePath);
    if (normalizedFolder.isEmpty() || key.isEmpty()) {
        return QString();
    }

    return markForImageKey(normalizedFolder, key);
}

QString ImageMarkManager::markForImageKey(const QString &folderPath,
                                          const QString &imageKey) const
{
    const QString normalizedFolder = normalizeFolderPath(folderPath);
    const QString key = normalizeStoredKey(imageKey);
    if (normalizedFolder.isEmpty() || key.isEmpty()) {
        return QString();
    }

    auto folderIt = m_folderMarks.constFind(normalizedFolder);
    if (folderIt == m_folderMarks.constEnd() || !folderIt->loaded) {
        return QString();
    }

    return folderIt->marks.value(key);
}

bool ImageMarkManager::setMarkForImage(const QString &folderPath,
                                       const QString &imagePath,
                                       const QString &category)
{
    const QString normalizedFolder = normalizeFolderPath(folderPath);
    const QString key = imageKeyForPath(normalizedFolder, imagePath);
    return setMarkForImageKey(normalizedFolder, key, category);
}

bool ImageMarkManager::setMarkForImageKey(const QString &folderPath,
                                          const QString &imageKey,
                                          const QString &category)
{
    if (!category.isEmpty() && !isValidCategory(category)) {
        return false;
    }

    const QString normalizedFolder = normalizeFolderPath(folderPath);
    const QString key = normalizeStoredKey(imageKey);
    if (normalizedFolder.isEmpty() || key.isEmpty()) {
        return false;
    }

    loadFolder(normalizedFolder);
    FolderMarks &folderMarks = m_folderMarks[normalizedFolder];
    const QString previous = folderMarks.marks.value(key);

    if (category.isEmpty()) {
        if (!folderMarks.marks.remove(key)) {
            return false;
        }
    } else {
        if (previous == category) {
            return false;
        }
        folderMarks.marks.insert(key, category);
    }

    if (!appendJournalEntry(normalizedFolder, key, category)) {
        if (previous.isEmpty()) {
            folderMarks.marks.remove(key);
        } else {
            folderMarks.marks.insert(key, previous);
        }
        return false;
    }

    emit markChanged(normalizedFolder, QDir(normalizedFolder).filePath(key), category);
    return true;
}

bool ImageMarkManager::clearMarkForImage(const QString &folderPath,
                                         const QString &imagePath)
{
    return setMarkForImage(folderPath, imagePath, QString());
}

QString ImageMarkManager::normalizeFolderPath(const QString &folderPath)
{
    return normalizePath(folderPath);
}

QString ImageMarkManager::normalizeImagePath(const QString &imagePath)
{
    return normalizePath(imagePath);
}

QString ImageMarkManager::normalizePath(const QString &path)
{
    const QString normalizedSeparators = QDir::fromNativeSeparators(path.trimmed());
    if (normalizedSeparators.isEmpty()) {
        return QString();
    }

    if (QDir::isAbsolutePath(normalizedSeparators)) {
        return QDir::cleanPath(normalizedSeparators);
    }

    return QDir::cleanPath(QDir::current().absoluteFilePath(normalizedSeparators));
}

QString ImageMarkManager::normalizeStoredKey(const QString &imageKey)
{
    const QString normalized = QDir::fromNativeSeparators(QDir::cleanPath(imageKey.trimmed()));
    if (normalized == QStringLiteral(".") || normalized.isEmpty()) {
        return QString();
    }
    return normalized;
}

QString ImageMarkManager::imageKeyForPath(const QString &folderPath,
                                          const QString &imagePath)
{
    const QString normalizedFolder = normalizeFolderPath(folderPath);
    const QString normalizedImage = normalizeImagePath(imagePath);
    return imageKeyFromNormalizedPath(normalizedFolder, normalizedImage);
}

QString ImageMarkManager::imageKeyFromNormalizedPath(const QString &normalizedFolder,
                                                     const QString &normalizedImage)
{
    if (normalizedFolder.isEmpty() || normalizedImage.isEmpty()) {
        return QString();
    }

    const QDir folderDir(normalizedFolder);
    QString relative = QDir::fromNativeSeparators(
        QDir::cleanPath(folderDir.relativeFilePath(normalizedImage)));

    if (relative == QStringLiteral("..") || relative.startsWith(QStringLiteral("../"))) {
        relative = QDir::fromNativeSeparators(normalizedImage);
    }

    return relative;
}

void ImageMarkManager::applyStoredMark(FolderMarks &folderMarks,
                                       const QString &imageKey,
                                       const QString &category)
{
    const QString key = normalizeStoredKey(imageKey);
    if (key.isEmpty()) {
        return;
    }

    if (category.isEmpty()) {
        folderMarks.marks.remove(key);
    } else if (isValidCategory(category)) {
        folderMarks.marks.insert(key, category);
    }
}

bool ImageMarkManager::loadSnapshot(const QString &normalizedFolderPath,
                                    FolderMarks &folderMarks) const
{
    QFile file(markFilePath(normalizedFolderPath));
    if (!file.exists()) {
        return true;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        return false;
    }

    const QJsonObject root = document.object();
    const QJsonObject marks = root.value(kMarksKey).toObject();
    for (auto it = marks.constBegin(); it != marks.constEnd(); ++it) {
        applyStoredMark(folderMarks, it.key(), it.value().toString());
    }

    return true;
}

bool ImageMarkManager::loadJournal(const QString &normalizedFolderPath,
                                   FolderMarks &folderMarks) const
{
    QFile file(markJournalPath(normalizedFolderPath));
    if (!file.exists()) {
        return true;
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }

        const QJsonDocument document = QJsonDocument::fromJson(line);
        if (!document.isObject()) {
            continue;
        }

        const QJsonObject entry = document.object();
        applyStoredMark(folderMarks,
                        entry.value(kPathKey).toString(),
                        entry.value(kCategoryKey).toString());
    }

    return true;
}

bool ImageMarkManager::appendJournalEntry(const QString &normalizedFolderPath,
                                          const QString &imageKey,
                                          const QString &category) const
{
    QJsonObject entry;
    entry.insert(kVersionKey, 1);
    entry.insert(kPathKey, normalizeStoredKey(imageKey));
    entry.insert(kCategoryKey, category);

    QByteArray line = QJsonDocument(entry).toJson(QJsonDocument::Compact);
    line.append('\n');

    QFile file(markJournalPath(normalizedFolderPath));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return false;
    }

    return file.write(line) == line.size() && file.flush();
}
