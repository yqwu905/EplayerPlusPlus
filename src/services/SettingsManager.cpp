#include "SettingsManager.h"

#include <QSettings>
#include <algorithm>

const QString SettingsManager::kFolderListKey = QStringLiteral("folders/list");
const QString SettingsManager::kComparisonThresholdKey = QStringLiteral("comparison/threshold");

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
