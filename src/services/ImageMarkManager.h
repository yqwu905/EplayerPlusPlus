#ifndef IMAGEMARKMANAGER_H
#define IMAGEMARKMANAGER_H

#include <QObject>
#include <QHash>
#include <QString>
#include <QStringList>

/**
 * @brief Persists per-image A/B/C/D marks in a JSON file inside each folder.
 */
class ImageMarkManager : public QObject
{
    Q_OBJECT

public:
    explicit ImageMarkManager(QObject *parent = nullptr);
    ~ImageMarkManager() override;

    static QStringList categories();
    static bool isValidCategory(const QString &category);
    static QString normalizeFolderPath(const QString &folderPath);
    static QString normalizeImagePath(const QString &imagePath);
    static QString imageKeyForPath(const QString &folderPath, const QString &imagePath);

    QString markFilePath(const QString &folderPath) const;
    QString markJournalPath(const QString &folderPath) const;
    bool loadFolder(const QString &folderPath);
    QString markForImage(const QString &folderPath, const QString &imagePath) const;
    QString markForImageKey(const QString &folderPath, const QString &imageKey) const;
    bool setMarkForImage(const QString &folderPath,
                         const QString &imagePath,
                         const QString &category);
    bool setMarkForImageKey(const QString &folderPath,
                            const QString &imageKey,
                            const QString &category);
    bool clearMarkForImage(const QString &folderPath, const QString &imagePath);

signals:
    void markChanged(const QString &folderPath,
                     const QString &imagePath,
                     const QString &category);

private:
    struct FolderMarks {
        bool loaded = false;
        QHash<QString, QString> marks;
    };

    static QString normalizePath(const QString &path);
    static QString normalizeStoredKey(const QString &imageKey);
    static QString imageKeyFromNormalizedPath(const QString &normalizedFolder,
                                              const QString &normalizedImage);
    static void applyStoredMark(FolderMarks &folderMarks,
                                const QString &imageKey,
                                const QString &category);
    bool loadSnapshot(const QString &normalizedFolderPath, FolderMarks &folderMarks) const;
    bool loadJournal(const QString &normalizedFolderPath, FolderMarks &folderMarks) const;
    bool appendJournalEntry(const QString &normalizedFolderPath,
                            const QString &imageKey,
                            const QString &category) const;

    QHash<QString, FolderMarks> m_folderMarks;
};

#endif // IMAGEMARKMANAGER_H
