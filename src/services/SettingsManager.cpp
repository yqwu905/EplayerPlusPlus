#include "SettingsManager.h"

#include <QSettings>
#include <QVariant>
#include <algorithm>

const QString SettingsManager::kFolderListKey = QStringLiteral("folders/list");
const QString SettingsManager::kComparisonThresholdKey = QStringLiteral("comparison/threshold");
const QString SettingsManager::kResizeToFirstImageKey = QStringLiteral("comparison/resize_to_first_image");
const QString SettingsManager::kIgnoreImageColorProfileKey = QStringLiteral("image/ignore_color_profile");
const QString SettingsManager::kSplitterSizesKey = QStringLiteral("ui/splitter_sizes");

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

bool SettingsManager::ignoreImageColorProfile() const
{
    QSettings settings;
    // Default ON: a pixel-diff tool wants two visually-identical files with
    // different color profiles to compare equal. Users who care about
    // color-managed rendering can opt out.
    return settings.value(kIgnoreImageColorProfileKey, true).toBool();
}

void SettingsManager::setIgnoreImageColorProfile(bool enabled)
{
    QSettings settings;
    settings.setValue(kIgnoreImageColorProfileKey, enabled);
}

QList<int> SettingsManager::splitterSizes() const
{
    QSettings settings;
    const QVariantList stored = settings.value(kSplitterSizesKey).toList();
    QList<int> sizes;
    sizes.reserve(stored.size());
    for (const QVariant &value : stored) {
        sizes.append(value.toInt());
    }
    return sizes;
}

void SettingsManager::setSplitterSizes(const QList<int> &sizes)
{
    QVariantList stored;
    stored.reserve(sizes.size());
    for (int size : sizes) {
        stored.append(size);
    }
    QSettings settings;
    settings.setValue(kSplitterSizesKey, stored);
}
