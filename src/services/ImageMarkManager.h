#ifndef IMAGEMARKMANAGER_H
#define IMAGEMARKMANAGER_H

#include <QFuture>
#include <QObject>
#include <QHash>
#include <QString>
#include <QStringList>

/**
 * @brief Persists per-image A/B/C/D marks without requiring image-folder writes.
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
    struct MarkEntry {
        QString category;
        qint64 timestamp = 0;
    };

    struct FolderMarks {
        bool loaded = false;
        QHash<QString, MarkEntry> marks;
    };

    static QString normalizePath(const QString &path);
    static QString normalizeStoredKey(const QString &imageKey);
    static QString imageKeyFromNormalizedPath(const QString &normalizedFolder,
                                              const QString &normalizedImage);
    static void applyStoredMark(FolderMarks &folderMarks,
                                const QString &imageKey,
                                const QString &category,
                                qint64 timestamp);
    bool loadSnapshot(const QString &normalizedFolderPath, FolderMarks &folderMarks) const;
    bool loadJournal(const QString &normalizedFolderPath, FolderMarks &folderMarks) const;
    bool loadJournalFile(const QString &journalPath, FolderMarks &folderMarks) const;
    QString localMarkJournalPath(const QString &normalizedFolderPath,
                                 bool createDirectory) const;
    void scheduleJournalWrite(const QString &normalizedFolderPath,
                              const QString &imageKey,
                              const QString &category,
                              qint64 timestamp);
    qint64 nextJournalTimestamp();
    void pruneFinishedWrites();

    QHash<QString, FolderMarks> m_folderMarks;
    QList<QFuture<void>> m_pendingWrites;
    qint64 m_lastJournalTimestamp = 0;
};

#endif // IMAGEMARKMANAGER_H
