#ifndef IMAGEMARKMANAGER_H
#define IMAGEMARKMANAGER_H

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QWaitCondition>

/**
 * @brief Persists per-image A/B/C/D marks without requiring image-folder writes.
 *
 * Persistence model
 * -----------------
 * Each folder owns two files (written either inside the image folder when
 * writable, or under an app-data fallback when it is not):
 *
 *   * `.imagecompare_marks.json`     — a periodically compacted *snapshot*
 *     of the current marks for the folder.
 *   * `.imagecompare_marks.journal`  — an append-only log of mark mutations.
 *
 * On load the snapshot is applied first, then the journal is replayed on top
 * (latest-timestamp wins). Mutations are appended to the journal synchronously
 * w.r.t. the in-memory map and asynchronously to disk via a single background
 * writer thread that serializes all I/O per process. Once the journal grows
 * past a threshold the writer atomically rewrites the snapshot and truncates
 * the journal (compaction). The writer always `fsync`s after each successful
 * write so that a power loss between flush and OS writeback cannot lose marks.
 *
 * Threading
 * ---------
 * All public methods are intended to be called from the GUI thread. Internal
 * state (`m_folderMarks`, accounting maps) is protected by a mutex so a
 * caller can safely read the in-memory map while the writer thread services
 * I/O. The writer is fully self-contained: every task it dequeues carries a
 * snapshot of the data it needs, so it does not call back into the owner.
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

    enum class TaskKind {
        AppendJournal,
        CompactSnapshot
    };

    // A self-contained unit of work for the writer thread. Carries every
    // path it needs to attempt the write, so the worker never reaches back
    // into the manager (which simplifies shutdown semantics).
    struct WriteTask {
        TaskKind kind = TaskKind::AppendJournal;
        // Primary destination (folder-local) and fallback (app-data) so the
        // worker can degrade gracefully without touching shared state.
        QString primaryPath;
        QString fallbackPath;
        // AppendJournal payload.
        QByteArray journalLine;
        // CompactSnapshot payload.
        QHash<QString, MarkEntry> snapshot;
        QString snapshotPath;
        QString journalPathToTruncate;
        QString fallbackJournalPathToTruncate;
        QString fallbackSnapshotPathToTruncate;
    };

    class Worker : public QThread
    {
    public:
        Worker();
        void enqueue(WriteTask task);
        void requestShutdown();

    protected:
        void run() override;

    private:
        void execute(const WriteTask &task);
        static bool appendJournal(const WriteTask &task);
        static bool writeSnapshot(const WriteTask &task);
        static bool writeJournalLine(const QString &path, const QByteArray &line);
        static bool flushToDisk(class QFile &file);
        static bool writeSnapshotFile(const QString &snapshotPath,
                                      const QHash<QString, MarkEntry> &marks);

        QMutex m_queueMutex;
        QWaitCondition m_queueCondition;
        QQueue<WriteTask> m_queue;
        bool m_shutdown = false;
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

    // Determines whether journal/snapshot writes should target the folder
    // itself or the per-folder local fallback directory.
    enum class StoreTarget { Folder, Local };
    StoreTarget chooseStoreTarget(const QString &normalizedFolderPath);
    QString localStoreDir(const QString &normalizedFolderPath, bool createDirectory) const;
    QString localMarkJournalPath(const QString &normalizedFolderPath,
                                 bool createDirectory) const;
    QString localSnapshotPath(const QString &normalizedFolderPath,
                              bool createDirectory) const;
    QString journalPathForStore(const QString &normalizedFolderPath, StoreTarget target) const;
    QString snapshotPathForStore(const QString &normalizedFolderPath, StoreTarget target) const;

    void scheduleJournalWrite(const QString &normalizedFolderPath,
                              const QString &imageKey,
                              const QString &category,
                              qint64 timestamp);
    // Caller must NOT hold m_mutex. Captures the current in-memory marks
    // under the mutex and enqueues a compaction task for the writer.
    void scheduleCompaction(const QString &normalizedFolderPath);

    qint64 nextJournalTimestamp();

    mutable QMutex m_mutex;
    QHash<QString, FolderMarks> m_folderMarks;
    QHash<QString, StoreTarget> m_storeTargetCache;
    // Bytes appended to the journal since the last compaction was scheduled,
    // per folder. Updated when an append is *enqueued* (an upper bound — the
    // worker may compress slightly when writing).
    QHash<QString, qint64> m_journalBytesSinceCompaction;
    qint64 m_lastJournalTimestamp = 0;
    Worker *m_worker = nullptr;
};

#endif // IMAGEMARKMANAGER_H
