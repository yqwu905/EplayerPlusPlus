#ifndef COMPARESESSION_H
#define COMPARESESSION_H

#include <QObject>
#include <QStringList>

/**
 * @brief Manages the current comparison session — a set of folders being compared.
 *
 * Supports up to 4 folders simultaneously. Provides add/remove/clear operations
 * and emits signals when the folder set changes.
 */
class CompareSession : public QObject
{
    Q_OBJECT

public:
    static constexpr int MaxFolders = 4;

    explicit CompareSession(QObject *parent = nullptr);
    ~CompareSession() override;

    /**
     * @brief Add a folder to the comparison session.
     * @param folderPath Absolute path of the folder.
     * @return true if added, false if max reached. Duplicates are allowed.
     */
    bool addFolder(const QString &folderPath);

    /**
     * @brief Remove a folder from the comparison session.
     * @param folderPath Absolute path of the folder.
     * @return true if removed, false if not found.
     */
    bool removeFolder(const QString &folderPath);

    /**
     * @brief Remove a folder by its index.
     * @param index Zero-based index.
     * @return true if removed.
     */
    bool removeFolderAt(int index);

    /**
     * @brief Clear all folders from the session.
     */
    void clear();

    /**
     * @brief Get the list of folders in the session.
     */
    QStringList folders() const;

    /**
     * @brief Get the number of folders in the session.
     */
    int folderCount() const;

    /**
     * @brief Check if the session contains a specific folder.
     */
    bool containsFolder(const QString &folderPath) const;

    /**
     * @brief Check if the session is full (MaxFolders reached).
     */
    bool isFull() const;

    /**
     * @brief Get the index of a folder in the session.
     * @return Index (0-based), or -1 if not found.
     */
    int indexOf(const QString &folderPath) const;

signals:
    /**
     * @brief Emitted when a folder is added.
     * @param folderPath The added folder path.
     * @param index The index at which it was added.
     */
    void folderAdded(const QString &folderPath, int index);

    /**
     * @brief Emitted when a folder is removed.
     * @param folderPath The removed folder path.
     * @param index The index from which it was removed.
     */
    void folderRemoved(const QString &folderPath, int index);

    /**
     * @brief Emitted when all folders are cleared.
     */
    void cleared();

    /**
     * @brief Emitted when the session changes (add, remove, or clear).
     */
    void sessionChanged();

private:
    QStringList m_folders;
};

#endif // COMPARESESSION_H
