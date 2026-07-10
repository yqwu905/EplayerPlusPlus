#ifndef IMAGEMARKMANAGER_H
#define IMAGEMARKMANAGER_H

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QWaitCondition>

/**
 * @brief Persists per-image A–F marks without requiring image-folder writes.
 *
 * Persistence model
 * -----------------
 * Each folder owns two canonical files under app data plus a best-effort mirror
 * beside the images when that folder is writable:
 *
 *   * `.imagecompare_marks.json`     — a periodically compacted *snapshot*
 *     of the current marks for the folder.
 *   * `.imagecompare_marks.journal`  — an append-only log of mark mutations.
 *
 * On load the snapshot is applied first, then the journal is replayed on top
 * (latest-timestamp wins). Local replay completes independently from remote
 * sidecar import. Mutations update memory immediately and go to two independent
 * writer queues: the canonical local queue and a best-effort remote mirror.
 * Thus a stuck share cannot block later local marks. Once the journal grows past
 * a threshold each queue compacts its own snapshot. Writers `fsync` successful
 * appends so a power loss between flush and OS writeback cannot lose local marks.
 *
 * Threading
 * ---------
 * All public methods are intended to be called from the GUI thread. Internal
 * state (`m_folderMarks`, accounting maps) is protected by a mutex so a
 * caller can safely read the in-memory map while writer threads service I/O.
 * Each writer is fully self-contained: every task it dequeues carries a
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
    static QString vlmSource();
    static QString normalizeFolderPath(const QString &folderPath);
    static QString normalizeImagePath(const QString &imagePath);
    static QString imageKeyForPath(const QString &folderPath, const QString &imagePath);

    struct MarkMetadata {
        QString category;
        QString source;
        QString reason;
    };

    QString markFilePath(const QString &folderPath) const;
    QString markJournalPath(const QString &folderPath) const;
    bool loadFolder(const QString &folderPath);
    // Starts snapshot/journal replay on a bounded background pool. Existing
    // in-memory marks remain readable and folderLoaded is emitted on merge.
    void loadFolderAsync(const QString &folderPath);
    QString markForImage(const QString &folderPath, const QString &imagePath) const;
    QString markForImageKey(const QString &folderPath, const QString &imageKey) const;
    MarkMetadata markMetadataForImage(const QString &folderPath, const QString &imagePath) const;
    MarkMetadata markMetadataForImageKey(const QString &folderPath, const QString &imageKey) const;
    // Takes one normalized-folder lookup and one mutex acquisition for the
    // complete folder. Models use this to cache paint/filter metadata instead
    // of locking the manager once per role and per row.
    QHash<QString, MarkMetadata> markMetadataForFolder(const QString &folderPath) const;
    // Returns {imageKey (folder-relative path) -> category} for every image that
    // currently carries an A–F mark. Unmarked entries are omitted. Returns an
    // empty hash if the folder has not been loaded.
    QHash<QString, QString> marksForFolder(const QString &folderPath) const;
    bool setMarkForImage(const QString &folderPath,
                         const QString &imagePath,
                         const QString &category);
    bool setMarkForImageKey(const QString &folderPath,
                            const QString &imageKey,
                            const QString &category);
    bool setVlmMarkForImage(const QString &folderPath,
                            const QString &imagePath,
                            const QString &category,
                            const QString &reason);
    bool setVlmMarkForImageKey(const QString &folderPath,
                               const QString &imageKey,
                               const QString &category,
                               const QString &reason);
    bool clearMarkForImage(const QString &folderPath, const QString &imagePath);

signals:
    void markChanged(const QString &folderPath,
                     const QString &imagePath,
                     const QString &category);
    void folderLoaded(const QString &folderPath);

private:
    struct MarkEntry {
        QString category;
        qint64 timestamp = 0;
        QString source;
        QString reason;
    };

    struct FolderMarks {
        bool loaded = false;
        bool loading = false;
        int pendingLoadParts = 0;
        quint64 loadGeneration = 0;
        QHash<QString, MarkEntry> marks;
        // Keys replayed from canonical local storage or modified this session.
        // A late best-effort remote import may fill missing keys but never
        // overwrite these, regardless of remote wall-clock timestamps.
        QSet<QString> localAuthoritativeKeys;
        // User mutations made while canonical local replay is in flight. They
        // overlay loaded data and are re-stamped when local replay completes;
        // late remote import is forbidden from overwriting these keys.
        QHash<QString, MarkEntry> pendingOverrides;
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
        // One destination only. Canonical local and best-effort remote writes
        // are submitted to separate Worker instances.
        QString primaryPath;
        // AppendJournal payload.
        QByteArray journalLine;
        // CompactSnapshot payload.
        QHash<QString, MarkEntry> snapshot;
        QString snapshotPath;
        QString journalPathToTruncate;
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
                                qint64 timestamp,
                                const QString &source = QString(),
                                const QString &reason = QString());

    static bool loadSnapshotFile(const QString &snapshotPath, FolderMarks &folderMarks);
    static bool loadJournalFile(const QString &journalPath, FolderMarks &folderMarks);

    QString localStoreDir(const QString &normalizedFolderPath, bool createDirectory) const;
    QString localMarkJournalPath(const QString &normalizedFolderPath,
                                 bool createDirectory) const;
    QString localSnapshotPath(const QString &normalizedFolderPath,
                              bool createDirectory) const;

    void scheduleJournalWrite(const QString &normalizedFolderPath,
                              const QString &imageKey,
                              const QString &category,
                              qint64 timestamp,
                              const QString &source = QString(),
                              const QString &reason = QString());
    // Caller must NOT hold m_mutex. Captures the current in-memory marks
    // under the mutex and enqueues a compaction task for the writer.
    void scheduleCompaction(const QString &normalizedFolderPath);

    qint64 nextJournalTimestamp();
    bool setMarkForImageKeyWithMetadata(const QString &folderPath,
                                        const QString &imageKey,
                                        const QString &category,
                                        const QString &source,
                                        const QString &reason);

    mutable QMutex m_mutex;
    QHash<QString, FolderMarks> m_folderMarks;
    // Bytes appended to the journal since the last compaction was scheduled,
    // per folder. Updated when an append is *enqueued* (an upper bound — the
    // worker may compress slightly when writing).
    QHash<QString, qint64> m_journalBytesSinceCompaction;
    qint64 m_lastJournalTimestamp = 0;
    Worker *m_worker = nullptr;
    Worker *m_remoteWorker = nullptr;
};

#endif // IMAGEMARKMANAGER_H
