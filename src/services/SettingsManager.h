#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <QObject>
#include <QStringList>
#include <QList>

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
     * @brief Whether embedded ICC / color-profile info on loaded images should be stripped.
     *
     * When enabled, QImage::setColorSpace(QColorSpace()) is applied after decode so two
     * visually-identical files with different color profiles render identically and
     * compare bit-for-bit. Pixel data is not transformed; only the profile tag is dropped.
     * Default: true — most users of a pixel-diff tool want determinism over color accuracy.
     */
    bool ignoreImageColorProfile() const;

    /**
     * @brief Enable or disable ignoring the embedded ICC / color-profile info on images.
     * @param enabled True to strip color profiles after decode.
     */
    void setIgnoreImageColorProfile(bool enabled);

    /**
     * @brief The persisted main-splitter panel widths (folder / browse / compare).
     *
     * Persisting the browse-panel width makes the user's chosen thumbnail zoom
     * level (which is a function of that width) survive across sessions.
     * @return The saved sizes, or an empty list if none were saved yet.
     */
    QList<int> splitterSizes() const;

    /**
     * @brief Persist the main-splitter panel widths.
     */
    void setSplitterSizes(const QList<int> &sizes);

private:
    static const QString kFolderListKey;
    static const QString kComparisonThresholdKey;
    static const QString kResizeToFirstImageKey;
    static const QString kIgnoreImageColorProfileKey;
    static const QString kSplitterSizesKey;
};

#endif // SETTINGSMANAGER_H
