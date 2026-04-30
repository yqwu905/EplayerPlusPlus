#include "ImageMarkManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace
{
const QString kMarkFileName = QStringLiteral(".imagecompare_marks.json");
const QString kVersionKey = QStringLiteral("version");
const QString kMarksKey = QStringLiteral("marks");
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

    QFile file(markFilePath(normalizedFolder));
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
        const QString category = it.value().toString();
        if (isValidCategory(category)) {
            folderMarks.marks.insert(QDir::fromNativeSeparators(QDir::cleanPath(it.key())),
                                     category);
        }
    }

    return true;
}

QString ImageMarkManager::markForImage(const QString &folderPath,
                                       const QString &imagePath) const
{
    const QString normalizedFolder = normalizeFolderPath(folderPath);
    const QString key = imageKey(normalizedFolder, imagePath);
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
    if (!category.isEmpty() && !isValidCategory(category)) {
        return false;
    }

    const QString normalizedFolder = normalizeFolderPath(folderPath);
    const QString normalizedImage = normalizeImagePath(imagePath);
    const QString key = imageKey(normalizedFolder, normalizedImage);
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

    if (!saveFolder(normalizedFolder)) {
        return false;
    }

    emit markChanged(normalizedFolder, normalizedImage, category);
    return true;
}

bool ImageMarkManager::clearMarkForImage(const QString &folderPath,
                                         const QString &imagePath)
{
    return setMarkForImage(folderPath, imagePath, QString());
}

QString ImageMarkManager::normalizeFolderPath(const QString &folderPath)
{
    if (folderPath.trimmed().isEmpty()) {
        return QString();
    }

    return QDir::cleanPath(QFileInfo(folderPath).absoluteFilePath());
}

QString ImageMarkManager::normalizeImagePath(const QString &imagePath)
{
    if (imagePath.trimmed().isEmpty()) {
        return QString();
    }

    return QDir::cleanPath(QFileInfo(imagePath).absoluteFilePath());
}

QString ImageMarkManager::imageKey(const QString &folderPath,
                                   const QString &imagePath)
{
    const QString normalizedFolder = normalizeFolderPath(folderPath);
    const QString normalizedImage = normalizeImagePath(imagePath);
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

bool ImageMarkManager::saveFolder(const QString &normalizedFolderPath) const
{
    auto it = m_folderMarks.constFind(normalizedFolderPath);
    if (it == m_folderMarks.constEnd()) {
        return false;
    }

    QJsonObject marksObject;
    for (auto markIt = it->marks.constBegin(); markIt != it->marks.constEnd(); ++markIt) {
        marksObject.insert(markIt.key(), markIt.value());
    }

    QJsonObject root;
    root.insert(kVersionKey, 1);
    root.insert(kMarksKey, marksObject);

    QSaveFile file(markFilePath(normalizedFolderPath));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}
