#include "SettingsManager.h"

#include <QSettings>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <algorithm>

const QString SettingsManager::kFolderListKey = QStringLiteral("folders/list");
const QString SettingsManager::kComparisonThresholdKey = QStringLiteral("comparison/threshold");
const QString SettingsManager::kResizeToFirstImageKey = QStringLiteral("comparison/resize_to_first_image");
const QString SettingsManager::kImageCategoryFileName = QStringLiteral(".image_categories.json");

SettingsManager::SettingsManager(QObject *parent)
    : QObject(parent)
{
}

SettingsManager::~SettingsManager() = default;

QStringList SettingsManager::loadFolderList() const
{
    QSettings settings;
    return settings.value(kFolderListKey).toStringList();
}

void SettingsManager::saveFolderList(const QStringList &folders)
{
    QSettings settings;
    settings.setValue(kFolderListKey, folders);
}

void SettingsManager::addFolder(const QString &folderPath)
{
    QStringList folders = loadFolderList();
    if (!folders.contains(folderPath)) {
        folders.append(folderPath);
        saveFolderList(folders);
    }
}

void SettingsManager::removeFolder(const QString &folderPath)
{
    QStringList folders = loadFolderList();
    folders.removeAll(folderPath);
    saveFolderList(folders);
}

void SettingsManager::clearFolderList()
{
    QSettings settings;
    settings.remove(kFolderListKey);
}

int SettingsManager::comparisonThreshold() const
{
    QSettings settings;
    return std::clamp(settings.value(kComparisonThresholdKey, 10).toInt(), 0, 255);
}

void SettingsManager::setComparisonThreshold(int threshold)
{
    QSettings settings;
    settings.setValue(kComparisonThresholdKey, std::clamp(threshold, 0, 255));
}

bool SettingsManager::resizeToFirstImageEnabled() const
{
    QSettings settings;
    return settings.value(kResizeToFirstImageKey, false).toBool();
}

void SettingsManager::setResizeToFirstImageEnabled(bool enabled)
{
    QSettings settings;
    settings.setValue(kResizeToFirstImageKey, enabled);
}

QHash<QString, int> SettingsManager::loadImageCategories(const QString &folderPath) const
{
    QHash<QString, int> categories;
    if (folderPath.isEmpty()) {
        return categories;
    }

    const QDir folderDir(folderPath);
    const QString filePath = folderDir.filePath(kImageCategoryFileName);
    QFile file(filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return categories;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return categories;
    }

    const QJsonObject root = doc.object();
    const QJsonObject items = root.value(QStringLiteral("items")).toObject();
    const QFileInfo folderInfo(folderPath);
    for (auto it = items.begin(); it != items.end(); ++it) {
        const int category = it.value().toInt(0);
        if (category < 1 || category > 4) {
            continue;
        }
        const QString absoluteImagePath = folderInfo.absoluteFilePath() + QDir::separator() + it.key();
        categories.insert(QDir::cleanPath(absoluteImagePath), category);
    }

    return categories;
}

void SettingsManager::saveImageCategories(
    const QString &folderPath,
    const QHash<QString, int> &categories) const
{
    if (folderPath.isEmpty()) {
        return;
    }

    const QDir folderDir(folderPath);
    if (!folderDir.exists()) {
        return;
    }

    QJsonObject items;
    const QFileInfo folderInfo(folderPath);
    const QString absoluteFolder = QDir::cleanPath(folderInfo.absoluteFilePath());

    for (auto it = categories.begin(); it != categories.end(); ++it) {
        const int category = it.value();
        if (category < 1 || category > 4) {
            continue;
        }

        const QString cleanedPath = QDir::cleanPath(it.key());
        QString relativePath = QDir(absoluteFolder).relativeFilePath(cleanedPath);
        if (relativePath.startsWith(QStringLiteral(".."))) {
            continue;
        }
        items.insert(relativePath, category);
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("items"), items);

    const QString filePath = folderDir.filePath(kImageCategoryFileName);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}
