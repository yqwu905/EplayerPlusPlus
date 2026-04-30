#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <QObject>
#include <QStringList>
#include <QHash>

/**
 * @brief Manages persistent application settings using QSettings.
 *
 * Handles saving and loading of folder lists and other application-level
 * configuration. Uses QSettings for cross-platform storage.
 */
class SettingsManager : public QObject
{
    Q_OBJECT

public:
    explicit SettingsManager(QObject *parent = nullptr);
    ~SettingsManager() override;

    /**
     * @brief Load the saved folder list from persistent storage.
     * @return List of folder paths.
     */
    QStringList loadFolderList() const;

    /**
     * @brief Save the folder list to persistent storage.
     * @param folders List of folder paths to save.
     */
    void saveFolderList(const QStringList &folders);

    /**
     * @brief Add a single folder to the saved list.
     * @param folderPath Folder path to add.
     */
    void addFolder(const QString &folderPath);

    /**
     * @brief Remove a single folder from the saved list.
     * @param folderPath Folder path to remove.
     */
    void removeFolder(const QString &folderPath);

    /**
     * @brief Clear all saved folders.
     */
    void clearFolderList();

    /**
     * @brief Get the comparison threshold value.
     * @return Threshold value (0-255), default is 10.
     */
    int comparisonThreshold() const;

    /**
     * @brief Set the comparison threshold value.
     * @param threshold Threshold value (0-255).
     */
    void setComparisonThreshold(int threshold);

    /**
     * @brief Whether non-primary compare images should be resized to the first image size.
     * @return True if resize alignment is enabled.
     */
    bool resizeToFirstImageEnabled() const;

    /**
     * @brief Enable or disable resizing non-primary compare images to the first image size.
     * @param enabled True to enable.
     */
    void setResizeToFirstImageEnabled(bool enabled);

    /**
     * @brief Load per-image category marks for a folder from JSON.
     * @param folderPath Folder path where the category JSON is stored.
     * @return Map of absolute image path -> category (1-4).
     */
    QHash<QString, int> loadImageCategories(const QString &folderPath) const;

    /**
     * @brief Save per-image category marks for a folder to JSON.
     * @param folderPath Folder path where the category JSON is stored.
     * @param categories Map of absolute image path -> category (1-4).
     */
    void saveImageCategories(const QString &folderPath,
                             const QHash<QString, int> &categories) const;

private:
    static const QString kFolderListKey;
    static const QString kComparisonThresholdKey;
    static const QString kResizeToFirstImageKey;
    static const QString kImageCategoryFileName;
};

#endif // SETTINGSMANAGER_H
