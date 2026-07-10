#include "ImageMarkManager.h"

#include <QCryptographicHash>
#include <QDeadlineTimer>
#include <QFutureWatcher>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThreadPool>
#include <QtConcurrent>

#include <utility>
#include <limits>

#ifdef Q_OS_WIN
#  include <io.h>
#  include <windows.h>
#else
#  include <unistd.h>
#endif

Q_LOGGING_CATEGORY(lcMarkManager, "imagecompare.markmanager")

namespace
{
const QString kMarkFileName = QStringLiteral(".imagecompare_marks.json");
const QString kMarkJournalFileName = QStringLiteral(".imagecompare_marks.journal");
const QString kVersionKey = QStringLiteral("version");
const QString kMarksKey = QStringLiteral("marks");
const QString kPathKey = QStringLiteral("path");
const QString kCategoryKey = QStringLiteral("category");
const QString kTimestampKey = QStringLiteral("timestamp");
const QString kSourceKey = QStringLiteral("source");
const QString kReasonKey = QStringLiteral("reason");
const QString kVlmSource = QStringLiteral("vlm");
constexpr qint64 kUntimedJournalEntryTimestamp = 0;

// Threshold at which an append triggers snapshot compaction. 64 KiB keeps
// load-time replay cheap while not thrashing the disk on every mark change.
constexpr qint64 kJournalCompactBytes = 64 * 1024;
// Hard cap on how long the destructor waits for the writer to drain. If the
// shared journal lives on a stuck network share the local fallback already
// has the data, so abandoning is safe.
constexpr int kShutdownWaitMs = 2000;

QThreadPool *markReadPool()
{
    // Canonical local app-data reads must never queue behind a stuck remote
    // sidecar. Keep a small independent pool for them.
    static QThreadPool *pool = [] {
        auto *p = new QThreadPool;
        p->setMaxThreadCount(2);
        p->setExpiryTimeout(30000);
        return p;
    }();
    return pool;
}

QThreadPool *remoteMarkReadPool()
{
    // Remote sidecar import is best effort and isolated: even if both workers
    // block inside the OS, canonical local marks still load and the UI updates.
    static QThreadPool *pool = [] {
        auto *p = new QThreadPool;
        p->setMaxThreadCount(2);
        p->setExpiryTimeout(30000);
        return p;
    }();
    return pool;
}
}

// ---------------------------------------------------------------------------
// Worker thread
// ---------------------------------------------------------------------------

ImageMarkManager::Worker::Worker()
{
}

void ImageMarkManager::Worker::enqueue(WriteTask task)
{
    QMutexLocker locker(&m_queueMutex);
    m_queue.enqueue(std::move(task));
    m_queueCondition.wakeOne();
}

void ImageMarkManager::Worker::requestShutdown()
{
    QMutexLocker locker(&m_queueMutex);
    m_shutdown = true;
    m_queueCondition.wakeAll();
}

void ImageMarkManager::Worker::run()
{
    for (;;) {
        WriteTask task;
        {
            QMutexLocker locker(&m_queueMutex);
            while (m_queue.isEmpty() && !m_shutdown) {
                m_queueCondition.wait(&m_queueMutex);
            }
            if (m_queue.isEmpty() && m_shutdown) {
                return;
            }
            task = m_queue.dequeue();

            // Coalesce a burst of marks for the same folder into one append and
            // one fsync. Ctrl-mark and VLM batches can enqueue hundreds of tiny
            // JSON lines; durability is unchanged because the combined payload
            // is flushed before the worker advances to the next task.
            if (task.kind == TaskKind::AppendJournal) {
                if (m_queue.isEmpty() && !m_shutdown) {
                    m_queueCondition.wait(&m_queueMutex, 2);
                }
                while (!m_queue.isEmpty()) {
                    const WriteTask &next = m_queue.head();
                    if (next.kind != TaskKind::AppendJournal ||
                        next.primaryPath != task.primaryPath) {
                        break;
                    }
                    task.journalLine.append(next.journalLine);
                    m_queue.dequeue();
                }
            }
        }
        execute(task);
    }
}

void ImageMarkManager::Worker::execute(const WriteTask &task)
{
    switch (task.kind) {
    case TaskKind::AppendJournal:
        appendJournal(task);
        break;
    case TaskKind::CompactSnapshot:
        writeSnapshot(task);
        break;
    }
}

bool ImageMarkManager::Worker::appendJournal(const WriteTask &task)
{
    if (writeJournalLine(task.primaryPath, task.journalLine)) {
        return true;
    }
    // Pre-format then log via "%s": the streaming qCWarning(category) form takes
    // no variadic argument, which trips -Wvariadic-macro-arguments-omitted under
    // -Wpedantic in C++17. "%s" + qPrintable keeps the variadic list non-empty
    // and is format-clean and portable on every compiler.
    qCWarning(lcMarkManager, "%s",
              qPrintable(QStringLiteral(
                  "ImageMarkManager: failed to append journal entry to %1")
                  .arg(task.primaryPath)));
    return false;
}

bool ImageMarkManager::Worker::writeJournalLine(const QString &path,
                                                const QByteArray &line)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return false;
    }
    if (file.write(line) != line.size()) {
        return false;
    }
    if (!file.flush()) {
        return false;
    }
    // fsync is best-effort: a failure here usually means the file system
    // does not support it (e.g. some FUSE mounts). The data is still in
    // the kernel page cache, which is strictly better than the previous
    // "flush only" behaviour.
    flushToDisk(file);
    file.close();
    return true;
}

bool ImageMarkManager::Worker::writeSnapshot(const WriteTask &task)
{
    if (!writeSnapshotFile(task.snapshotPath, task.snapshot)) {
        qCWarning(lcMarkManager, "%s",
                  qPrintable(QStringLiteral("ImageMarkManager: snapshot write failed for %1")
                                 .arg(task.snapshotPath)));
        return false;
    }

    // Snapshot is committed; the journal is now redundant.
    if (!task.journalPathToTruncate.isEmpty()) {
        QFile::remove(task.journalPathToTruncate);
    }
    return true;
}

bool ImageMarkManager::Worker::writeSnapshotFile(
    const QString &snapshotPath,
    const QHash<QString, MarkEntry> &marks)
{
    if (snapshotPath.isEmpty()) {
        return false;
    }
    QFileInfo info(snapshotPath);
    if (info.absolutePath().isEmpty()) {
        return false;
    }
    QDir().mkpath(info.absolutePath());

    QJsonObject marksObject;
    for (auto it = marks.constBegin(); it != marks.constEnd(); ++it) {
        // Persist empty-category entries too: they encode an explicit "clear"
        // whose timestamp must beat any older mark in stray journals.
        QJsonObject entry;
        entry.insert(kCategoryKey, it->category);
        entry.insert(kTimestampKey, it->timestamp);
        if (!it->source.isEmpty()) {
            entry.insert(kSourceKey, it->source);
        }
        if (!it->reason.isEmpty()) {
            entry.insert(kReasonKey, it->reason);
        }
        marksObject.insert(it.key(), entry);
    }
    QJsonObject root;
    root.insert(kVersionKey, 2);
    root.insert(kMarksKey, marksObject);

    QSaveFile file(snapshotPath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (file.write(payload) != payload.size()) {
        file.cancelWriting();
        return false;
    }
    // QSaveFile::commit() flushes and renames the temp file over the target
    // atomically. We cannot reach the underlying fd for an extra fsync, but
    // commit() on POSIX calls fsync internally before the rename.
    return file.commit();
}

bool ImageMarkManager::Worker::flushToDisk(QFile &file)
{
    if (!file.isOpen()) {
        return false;
    }
    const int handle = file.handle();
    if (handle < 0) {
        return false;
    }
#ifdef Q_OS_WIN
    HANDLE osHandle = reinterpret_cast<HANDLE>(_get_osfhandle(handle));
    if (osHandle == INVALID_HANDLE_VALUE) {
        return false;
    }
    return FlushFileBuffers(osHandle) != 0;
#else
    return ::fsync(handle) == 0;
#endif
}

// ---------------------------------------------------------------------------
// Construction / shutdown
// ---------------------------------------------------------------------------

ImageMarkManager::ImageMarkManager(QObject *parent)
    : QObject(parent)
{
    m_worker = new Worker;
    m_remoteWorker = new Worker;
    m_worker->start();
    m_remoteWorker->start();
}

ImageMarkManager::~ImageMarkManager()
{
    if (!m_worker && !m_remoteWorker) {
        return;
    }

    // Compact only folders this manager actually mutated. Local and remote
    // queues are independent, so a stuck sidecar cannot delay the canonical
    // local snapshot beyond the shared bounded shutdown deadline.
    QList<QString> foldersToCompact;
    {
        QMutexLocker locker(&m_mutex);
        foldersToCompact.reserve(m_journalBytesSinceCompaction.size());
        for (auto it = m_journalBytesSinceCompaction.constBegin();
             it != m_journalBytesSinceCompaction.constEnd(); ++it) {
            if (it.value() <= 0) {
                continue;
            }
            const auto folderIt = m_folderMarks.constFind(it.key());
            if (folderIt != m_folderMarks.constEnd() && folderIt->loaded) {
                foldersToCompact.append(it.key());
            }
        }
    }
    for (const QString &folder : std::as_const(foldersToCompact)) {
        scheduleCompaction(folder);
    }

    if (m_worker) m_worker->requestShutdown();
    if (m_remoteWorker) m_remoteWorker->requestShutdown();

    QDeadlineTimer deadline(kShutdownWaitMs);
    auto drainWorker = [&deadline](Worker *&worker, const QString &label) {
        if (!worker) {
            return;
        }
        const unsigned long remaining = static_cast<unsigned long>(
            qMax<qint64>(0, deadline.remainingTime()));
        if (!worker->wait(remaining)) {
            qCWarning(lcMarkManager, "%s",
                      qPrintable(QStringLiteral(
                          "ImageMarkManager: %1 writer exceeded the shared %2 ms "
                          "shutdown deadline; abandoning its self-contained thread. "
                          "The canonical local journal was queued independently.")
                          .arg(label)
                          .arg(kShutdownWaitMs)));
            // Deleting a running QThread is fatal. Workers never reference the
            // manager, so a stuck remote filesystem operation can be detached.
            worker = nullptr;
            return;
        }
        delete worker;
        worker = nullptr;
    };
    // Give the canonical local queue the deadline first; remote sidecar
    // replication is explicitly best effort.
    drainWorker(m_worker, QStringLiteral("local"));
    drainWorker(m_remoteWorker, QStringLiteral("remote"));
}

// ---------------------------------------------------------------------------
// Categories & path helpers
// ---------------------------------------------------------------------------

QStringList ImageMarkManager::categories()
{
    return {
        QStringLiteral("A"),
        QStringLiteral("B"),
        QStringLiteral("C"),
        QStringLiteral("D"),
        QStringLiteral("E"),
        QStringLiteral("F")
    };
}

bool ImageMarkManager::isValidCategory(const QString &category)
{
    return categories().contains(category);
}

QString ImageMarkManager::vlmSource()
{
    return kVlmSource;
}

QString ImageMarkManager::markFilePath(const QString &folderPath) const
{
    return QDir(normalizeFolderPath(folderPath)).filePath(kMarkFileName);
}

QString ImageMarkManager::markJournalPath(const QString &folderPath) const
{
    return QDir(normalizeFolderPath(folderPath)).filePath(kMarkJournalFileName);
}

QString ImageMarkManager::normalizeFolderPath(const QString &folderPath)
{
    return normalizePath(folderPath);
}

QString ImageMarkManager::normalizeImagePath(const QString &imagePath)
{
    return normalizePath(imagePath);
}

QString ImageMarkManager::normalizePath(const QString &path)
{
    const QString normalizedSeparators = QDir::fromNativeSeparators(path.trimmed());
    if (normalizedSeparators.isEmpty()) {
        return QString();
    }

    if (QDir::isAbsolutePath(normalizedSeparators)) {
        return QDir::cleanPath(normalizedSeparators);
    }

    return QDir::cleanPath(QDir::current().absoluteFilePath(normalizedSeparators));
}

QString ImageMarkManager::normalizeStoredKey(const QString &imageKey)
{
    const QString normalized = QDir::fromNativeSeparators(QDir::cleanPath(imageKey.trimmed()));
    if (normalized == QStringLiteral(".") || normalized.isEmpty()) {
        return QString();
    }
    return normalized;
}

QString ImageMarkManager::imageKeyForPath(const QString &folderPath,
                                          const QString &imagePath)
{
    const QString normalizedFolder = normalizeFolderPath(folderPath);
    const QString normalizedImage = normalizeImagePath(imagePath);
    return imageKeyFromNormalizedPath(normalizedFolder, normalizedImage);
}

QString ImageMarkManager::imageKeyFromNormalizedPath(const QString &normalizedFolder,
                                                     const QString &normalizedImage)
{
    if (normalizedFolder.isEmpty() || normalizedImage.isEmpty()) {
        return QString();
    }

    const QDir folderDir(normalizedFolder);
    QString relative = QDir::fromNativeSeparators(
        QDir::cleanPath(folderDir.relativeFilePath(normalizedImage)));

    if (relative == QStringLiteral("..") || relative.startsWith(QStringLiteral("../"))) {
        relative = QDir::fromNativeSeparators(normalizedImage);
    }

    return relative;
}

void ImageMarkManager::applyStoredMark(FolderMarks &folderMarks,
                                       const QString &imageKey,
                                       const QString &category,
                                       qint64 timestamp,
                                       const QString &source,
                                       const QString &reason)
{
    const QString key = normalizeStoredKey(imageKey);
    if (key.isEmpty()) {
        return;
    }

    const auto existing = folderMarks.marks.constFind(key);
    if (existing != folderMarks.marks.constEnd() &&
        existing->timestamp > timestamp) {
        return;
    }

    if (category.isEmpty()) {
        folderMarks.marks.insert(key, MarkEntry{QString(), timestamp, QString(), QString()});
    } else if (isValidCategory(category)) {
        const QString storedSource = source.trimmed();
        folderMarks.marks.insert(key,
                                 MarkEntry{category,
                                           timestamp,
                                           storedSource,
                                           storedSource.isEmpty() ? QString() : reason});
    }
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

bool ImageMarkManager::loadFolder(const QString &folderPath)
{
    const QString normalizedFolder = normalizeFolderPath(folderPath);
    if (normalizedFolder.isEmpty()) {
        return false;
    }

    // Hold the mutex for the entire load so that any concurrent
    // setMarkForImage caller waits until we are done. Loading is usually a
    // one-shot operation per folder (the `loaded` short-circuit below makes
    // subsequent calls cheap) so the contention window is acceptable, and
    // releasing the lock around disk I/O would race with concurrent mutators
    // and lose the newly-inserted mark when we overwrite the entry.
    QMutexLocker locker(&m_mutex);
    FolderMarks &slot = m_folderMarks[normalizedFolder];
    if (slot.loaded) {
        return true;
    }
    ++slot.loadGeneration;
    slot.loaded = true;
    slot.loading = false;
    slot.pendingLoadParts = 0;
    FolderMarks remoteData;
    loadSnapshotFile(markFilePath(normalizedFolder), remoteData);
    loadJournalFile(markJournalPath(normalizedFolder), remoteData);
    FolderMarks localData;
    loadSnapshotFile(localSnapshotPath(normalizedFolder, false), localData);
    loadJournalFile(localMarkJournalPath(normalizedFolder, false), localData);

    slot.marks = remoteData.marks;
    slot.localAuthoritativeKeys.clear();
    for (auto it = localData.marks.constBegin(); it != localData.marks.constEnd(); ++it) {
        // Canonical local state wins even if a remote machine's wall clock was
        // ahead. Remote replay remains an import path for locally-missing keys.
        slot.marks.insert(it.key(), it.value());
        slot.localAuthoritativeKeys.insert(it.key());
    }

    // Guard last-writer-wins against a system-clock rollback across restarts.
    // nextJournalTimestamp() keeps timestamps monotonic within a session but
    // resets to the wall clock on a fresh process; if the clock went backwards
    // since the previous run, a new mark could be stamped older than one already
    // persisted and lose on replay. Raise the floor to every timestamp loaded
    // here so freshly minted ones always win. (m_lastJournalTimestamp is
    // guarded by m_mutex, held here.)
    for (auto it = remoteData.marks.constBegin(); it != remoteData.marks.constEnd(); ++it) {
        m_lastJournalTimestamp = qMax(m_lastJournalTimestamp, it->timestamp);
    }
    for (auto it = localData.marks.constBegin(); it != localData.marks.constEnd(); ++it) {
        m_lastJournalTimestamp = qMax(m_lastJournalTimestamp, it->timestamp);
    }

    QList<QPair<QString, MarkEntry>> rewrites;
    rewrites.reserve(slot.pendingOverrides.size());
    for (auto it = slot.pendingOverrides.begin();
         it != slot.pendingOverrides.end(); ++it) {
        MarkEntry entry = it.value();
        entry.timestamp = nextJournalTimestamp();
        slot.marks.insert(it.key(), entry);
        slot.localAuthoritativeKeys.insert(it.key());
        rewrites.append({it.key(), entry});
    }
    slot.pendingOverrides.clear();
    const bool compactAfterMerge =
        m_journalBytesSinceCompaction.value(normalizedFolder) >=
        kJournalCompactBytes;
    locker.unlock();
    for (const auto &rewrite : std::as_const(rewrites)) {
        scheduleJournalWrite(normalizedFolder,
                             rewrite.first,
                             rewrite.second.category,
                             rewrite.second.timestamp,
                             rewrite.second.source,
                             rewrite.second.reason);
    }
    if (compactAfterMerge) {
        scheduleCompaction(normalizedFolder);
    }
    emit folderLoaded(normalizedFolder);
    return true;
}

void ImageMarkManager::loadFolderAsync(const QString &folderPath)
{
    const QString normalizedFolder = normalizeFolderPath(folderPath);
    if (normalizedFolder.isEmpty()) {
        return;
    }

    quint64 generation = 0;
    {
        QMutexLocker locker(&m_mutex);
        FolderMarks &slot = m_folderMarks[normalizedFolder];
        if (slot.loaded || slot.loading) {
            return;
        }
        slot.loading = true;
        slot.pendingLoadParts = 2;
        generation = ++slot.loadGeneration;
    }

    // Resolve paths before dispatch. Local canonical state and remote sidecars
    // use independent pools, so a hung share cannot delay local marks or consume
    // the workers that load them.
    const QString localSnapshot = localSnapshotPath(normalizedFolder, false);
    const QString localJournal = localMarkJournalPath(normalizedFolder, false);
    const QString remoteSnapshot = markFilePath(normalizedFolder);
    const QString remoteJournal = markJournalPath(normalizedFolder);

    auto startPart = [this, normalizedFolder, generation](
                         QThreadPool *pool,
                         const QString &snapshotPath,
                         const QString &journalPath,
                         bool localPart) {
        auto *watcher = new QFutureWatcher<FolderMarks>(this);
        connect(watcher, &QFutureWatcher<FolderMarks>::finished,
                this, [this, watcher, normalizedFolder, generation, localPart]() {
            const FolderMarks loaded = watcher->result();
            watcher->deleteLater();

            QList<QPair<QString, MarkEntry>> rewrites;
            bool notify = false;
            bool compactAfterMerge = false;
            {
                QMutexLocker locker(&m_mutex);
                FolderMarks &slot = m_folderMarks[normalizedFolder];
                if (slot.loadGeneration != generation) {
                    return;
                }
                for (auto it = loaded.marks.constBegin();
                     it != loaded.marks.constEnd(); ++it) {
                    m_lastJournalTimestamp = qMax(m_lastJournalTimestamp, it->timestamp);
                    if (localPart) {
                        // Local is canonical: force it over an earlier-arriving
                        // remote value even when the remote clock is ahead.
                        slot.marks.insert(it.key(), it.value());
                        slot.localAuthoritativeKeys.insert(it.key());
                    } else if (!slot.localAuthoritativeKeys.contains(it.key()) &&
                               !slot.pendingOverrides.contains(it.key())) {
                        applyStoredMark(slot,
                                        it.key(),
                                        it->category,
                                        it->timestamp,
                                        it->source,
                                        it->reason);
                    }
                }
                slot.pendingLoadParts = qMax(0, slot.pendingLoadParts - 1);
                if (localPart) {
                    slot.loaded = true;
                    slot.loading = false;
                    rewrites.reserve(slot.pendingOverrides.size());
                    for (auto it = slot.pendingOverrides.begin();
                         it != slot.pendingOverrides.end(); ++it) {
                        MarkEntry entry = it.value();
                        entry.timestamp = nextJournalTimestamp();
                        slot.marks.insert(it.key(), entry);
                        slot.localAuthoritativeKeys.insert(it.key());
                        rewrites.append({it.key(), entry});
                    }
                    slot.pendingOverrides.clear();
                    compactAfterMerge =
                        m_journalBytesSinceCompaction.value(normalizedFolder) >=
                        kJournalCompactBytes;
                }
                notify = slot.loaded;
            }

            // Re-stamp mutations made during replay above every loaded timestamp
            // and append the compensating record outside the state mutex.
            for (const auto &rewrite : std::as_const(rewrites)) {
                scheduleJournalWrite(normalizedFolder,
                                     rewrite.first,
                                     rewrite.second.category,
                                     rewrite.second.timestamp,
                                     rewrite.second.source,
                                     rewrite.second.reason);
            }
            if (compactAfterMerge) {
                scheduleCompaction(normalizedFolder);
            }
            if (notify) {
                emit folderLoaded(normalizedFolder);
            }
        });

        watcher->setFuture(QtConcurrent::run(
            pool,
            [snapshotPath, journalPath]() {
                FolderMarks loaded;
                loadSnapshotFile(snapshotPath, loaded);
                loadJournalFile(journalPath, loaded);
                return loaded;
            }));
    };

    startPart(markReadPool(), localSnapshot, localJournal, true);
    startPart(remoteMarkReadPool(), remoteSnapshot, remoteJournal, false);
}

bool ImageMarkManager::loadSnapshotFile(const QString &snapshotPath,
                                        FolderMarks &folderMarks)
{
    if (snapshotPath.isEmpty()) {
        return false;
    }
    QFile file(snapshotPath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        return false;
    }
    const QJsonObject marks = document.object().value(kMarksKey).toObject();
    for (auto it = marks.constBegin(); it != marks.constEnd(); ++it) {
        const QJsonValue value = it.value();
        // New-format snapshot stores metadata per entry; v1 stored a category
        // string. Accept both during background and synchronous replay.
        if (value.isObject()) {
            const QJsonObject entry = value.toObject();
            applyStoredMark(folderMarks,
                            it.key(),
                            entry.value(kCategoryKey).toString(),
                            entry.value(kTimestampKey).toVariant().toLongLong(),
                            entry.value(kSourceKey).toString(),
                            entry.value(kReasonKey).toString());
        } else {
            applyStoredMark(folderMarks,
                            it.key(),
                            value.toString(),
                            kUntimedJournalEntryTimestamp);
        }
    }
    return true;
}

bool ImageMarkManager::loadJournalFile(const QString &journalPath,
                                       FolderMarks &folderMarks)
{
    if (journalPath.isEmpty()) {
        return true;
    }

    QFile file(journalPath);
    if (!file.exists()) {
        return true;
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }

        const QJsonDocument document = QJsonDocument::fromJson(line);
        if (!document.isObject()) {
            continue;
        }

        const QJsonObject entry = document.object();
        applyStoredMark(folderMarks,
                        entry.value(kPathKey).toString(),
                        entry.value(kCategoryKey).toString(),
                        entry.value(kTimestampKey).toVariant().toLongLong(),
                        entry.value(kSourceKey).toString(),
                        entry.value(kReasonKey).toString());
    }

    return true;
}

// ---------------------------------------------------------------------------
// Reads
// ---------------------------------------------------------------------------

QString ImageMarkManager::markForImage(const QString &folderPath,
                                       const QString &imagePath) const
{
    const QString normalizedFolder = normalizeFolderPath(folderPath);
    const QString key = imageKeyForPath(normalizedFolder, imagePath);
    if (normalizedFolder.isEmpty() || key.isEmpty()) {
        return QString();
    }

    return markForImageKey(normalizedFolder, key);
}

QString ImageMarkManager::markForImageKey(const QString &folderPath,
                                          const QString &imageKey) const
{
    const QString normalizedFolder = normalizeFolderPath(folderPath);
    const QString key = normalizeStoredKey(imageKey);
    if (normalizedFolder.isEmpty() || key.isEmpty()) {
        return QString();
    }

    QMutexLocker locker(&m_mutex);
    auto folderIt = m_folderMarks.constFind(normalizedFolder);
    if (folderIt == m_folderMarks.constEnd()) {
        return QString();
    }

    const auto pendingIt = folderIt->pendingOverrides.constFind(key);
    if (pendingIt != folderIt->pendingOverrides.constEnd()) {
        return pendingIt->category;
    }
    if (!folderIt->loaded) {
        return QString();
    }
    const auto markIt = folderIt->marks.constFind(key);
    if (markIt == folderIt->marks.constEnd()) {
        return QString();
    }

    return markIt->category;
}

ImageMarkManager::MarkMetadata ImageMarkManager::markMetadataForImage(
    const QString &folderPath,
    const QString &imagePath) const
{
    const QString normalizedFolder = normalizeFolderPath(folderPath);
    const QString key = imageKeyForPath(normalizedFolder, imagePath);
    if (normalizedFolder.isEmpty() || key.isEmpty()) {
        return {};
    }

    return markMetadataForImageKey(normalizedFolder, key);
}

ImageMarkManager::MarkMetadata ImageMarkManager::markMetadataForImageKey(
    const QString &folderPath,
    const QString &imageKey) const
{
    const QString normalizedFolder = normalizeFolderPath(folderPath);
    const QString key = normalizeStoredKey(imageKey);
    if (normalizedFolder.isEmpty() || key.isEmpty()) {
        return {};
    }

    QMutexLocker locker(&m_mutex);
    auto folderIt = m_folderMarks.constFind(normalizedFolder);
    if (folderIt == m_folderMarks.constEnd()) {
        return {};
    }

    const auto pendingIt = folderIt->pendingOverrides.constFind(key);
    if (pendingIt != folderIt->pendingOverrides.constEnd()) {
        return MarkMetadata{pendingIt->category, pendingIt->source, pendingIt->reason};
    }
    if (!folderIt->loaded) {
        return {};
    }
    const auto markIt = folderIt->marks.constFind(key);
    if (markIt == folderIt->marks.constEnd()) {
        return {};
    }

    return MarkMetadata{markIt->category, markIt->source, markIt->reason};
}

QHash<QString, ImageMarkManager::MarkMetadata>
ImageMarkManager::markMetadataForFolder(const QString &folderPath) const
{
    const QString normalizedFolder = normalizeFolderPath(folderPath);
    if (normalizedFolder.isEmpty()) {
        return {};
    }

    QMutexLocker locker(&m_mutex);
    const auto folderIt = m_folderMarks.constFind(normalizedFolder);
    if (folderIt == m_folderMarks.constEnd()) {
        return {};
    }

    QHash<QString, MarkMetadata> result;
    result.reserve(folderIt->marks.size() + folderIt->pendingOverrides.size());
    if (folderIt->loaded) {
        for (auto it = folderIt->marks.constBegin(); it != folderIt->marks.constEnd(); ++it) {
            if (it->category.isEmpty() && it->source.isEmpty() && it->reason.isEmpty()) {
                continue;
            }
            result.insert(it.key(), MarkMetadata{it->category, it->source, it->reason});
        }
    }
    for (auto it = folderIt->pendingOverrides.constBegin();
         it != folderIt->pendingOverrides.constEnd(); ++it) {
        if (it->category.isEmpty() && it->source.isEmpty() && it->reason.isEmpty()) {
            result.remove(it.key());
        } else {
            result.insert(it.key(), MarkMetadata{it->category, it->source, it->reason});
        }
    }
    return result;
}

QHash<QString, QString> ImageMarkManager::marksForFolder(const QString &folderPath) const
{
    const QString normalizedFolder = normalizeFolderPath(folderPath);
    if (normalizedFolder.isEmpty()) {
        return {};
    }

    QMutexLocker locker(&m_mutex);
    auto folderIt = m_folderMarks.constFind(normalizedFolder);
    if (folderIt == m_folderMarks.constEnd()) {
        return {};
    }

    QHash<QString, QString> result;
    if (folderIt->loaded) {
        for (auto it = folderIt->marks.constBegin(); it != folderIt->marks.constEnd(); ++it) {
            if (!it->category.isEmpty()) {
                result.insert(it.key(), it->category);
            }
        }
    }
    for (auto it = folderIt->pendingOverrides.constBegin();
         it != folderIt->pendingOverrides.constEnd(); ++it) {
        if (it->category.isEmpty()) {
            result.remove(it.key());
        } else {
            result.insert(it.key(), it->category);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Mutations
// ---------------------------------------------------------------------------

bool ImageMarkManager::setMarkForImage(const QString &folderPath,
                                       const QString &imagePath,
                                       const QString &category)
{
    const QString normalizedFolder = normalizeFolderPath(folderPath);
    const QString key = imageKeyForPath(normalizedFolder, imagePath);
    return setMarkForImageKey(normalizedFolder, key, category);
}

bool ImageMarkManager::setMarkForImageKey(const QString &folderPath,
                                          const QString &imageKey,
                                          const QString &category)
{
    return setMarkForImageKeyWithMetadata(folderPath, imageKey, category, QString(), QString());
}

bool ImageMarkManager::setVlmMarkForImage(const QString &folderPath,
                                          const QString &imagePath,
                                          const QString &category,
                                          const QString &reason)
{
    const QString normalizedFolder = normalizeFolderPath(folderPath);
    const QString key = imageKeyForPath(normalizedFolder, imagePath);
    return setVlmMarkForImageKey(normalizedFolder, key, category, reason);
}

bool ImageMarkManager::setVlmMarkForImageKey(const QString &folderPath,
                                             const QString &imageKey,
                                             const QString &category,
                                             const QString &reason)
{
    return setMarkForImageKeyWithMetadata(folderPath,
                                          imageKey,
                                          category,
                                          kVlmSource,
                                          reason);
}

bool ImageMarkManager::setMarkForImageKeyWithMetadata(const QString &folderPath,
                                                      const QString &imageKey,
                                                      const QString &category,
                                                      const QString &source,
                                                      const QString &reason)
{
    if (!category.isEmpty() && !isValidCategory(category)) {
        return false;
    }

    const QString normalizedFolder = normalizeFolderPath(folderPath);
    const QString key = normalizeStoredKey(imageKey);
    if (normalizedFolder.isEmpty() || key.isEmpty()) {
        return false;
    }

    const QString storedSource = category.isEmpty() ? QString() : source.trimmed();
    const QString storedReason = storedSource.isEmpty() ? QString() : reason;

    // Never replay sidecar files on the caller/UI thread. The mutation is kept
    // as an overlay until local + remote background replay completes.
    loadFolderAsync(normalizedFolder);

    qint64 timestamp = 0;
    qint64 estimatedBytes = 0;
    bool shouldCompact = false;
    {
        QMutexLocker locker(&m_mutex);
        FolderMarks &folderMarks = m_folderMarks[normalizedFolder];
        const bool storeAsPending = folderMarks.loading || !folderMarks.loaded;
        const MarkEntry *previousEntry = nullptr;
        const auto pendingIt = folderMarks.pendingOverrides.constFind(key);
        if (pendingIt != folderMarks.pendingOverrides.constEnd()) {
            previousEntry = &pendingIt.value();
        } else if (folderMarks.loaded) {
            const auto loadedIt = folderMarks.marks.constFind(key);
            if (loadedIt != folderMarks.marks.constEnd()) {
                previousEntry = &loadedIt.value();
            }
        }
        const QString previous = previousEntry ? previousEntry->category : QString();
        const QString previousSource = previousEntry ? previousEntry->source : QString();
        const QString previousReason = previousEntry ? previousEntry->reason : QString();

        if (category.isEmpty()) {
            // While replay is pending, an absent previous entry is unknown: keep
            // an explicit clear tombstone so a persisted mark cannot reappear.
            if ((previousEntry && previous.isEmpty()) ||
                (!storeAsPending && !previousEntry)) {
                return false;
            }
        } else if (previous == category &&
                   previousSource == storedSource &&
                   previousReason == storedReason) {
            return false;
        }

        timestamp = nextJournalTimestamp();
        const MarkEntry updated{category, timestamp, storedSource, storedReason};
        if (storeAsPending) {
            folderMarks.pendingOverrides.insert(key, updated);
        } else {
            folderMarks.marks.insert(key, updated);
        }
        folderMarks.localAuthoritativeKeys.insert(key);

        // Cheap upper-bound estimate of how much we are about to write. The
        // worker will write the same shape so this is accurate enough for
        // compaction triggering.
        estimatedBytes = static_cast<qint64>(key.size())
                         + static_cast<qint64>(category.size())
                         + static_cast<qint64>(storedSource.size())
                         + static_cast<qint64>(storedReason.size())
                         + 64; // JSON overhead + timestamp + newline.
        qint64 &accum = m_journalBytesSinceCompaction[normalizedFolder];
        accum += estimatedBytes;
        if (accum >= kJournalCompactBytes && !storeAsPending) {
            shouldCompact = true;
            accum = 0;
        }
    }

    scheduleJournalWrite(normalizedFolder, key, category, timestamp, storedSource, storedReason);
    if (shouldCompact) {
        scheduleCompaction(normalizedFolder);
    }
    emit markChanged(normalizedFolder, QDir(normalizedFolder).filePath(key), category);
    return true;
}

bool ImageMarkManager::clearMarkForImage(const QString &folderPath,
                                         const QString &imagePath)
{
    return setMarkForImage(folderPath, imagePath, QString());
}

QString ImageMarkManager::localStoreDir(const QString &normalizedFolderPath,
                                        bool createDirectory) const
{
    if (normalizedFolderPath.isEmpty()) {
        return QString();
    }

    const QByteArray folderHash = QCryptographicHash::hash(
        normalizedFolderPath.toUtf8(),
        QCryptographicHash::Sha256).toHex();

    QStringList roots;
    const QString appDataRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appDataRoot.isEmpty()) {
        roots.append(appDataRoot);
    }
    roots.append(QDir(QDir::tempPath()).filePath(QStringLiteral("ImageCompare")));

    QString fallback;
    for (const QString &root : roots) {
        const QString storeDir = QDir(root).filePath(
            QStringLiteral("image-marks/%1").arg(QString::fromLatin1(folderHash)));
        if (!createDirectory) {
            if (QFileInfo::exists(storeDir)) {
                return storeDir;
            }
            if (fallback.isEmpty()) {
                fallback = storeDir;
            }
            continue;
        }
        if (QDir().mkpath(storeDir)) {
            return storeDir;
        }
    }
    return fallback;
}

QString ImageMarkManager::localMarkJournalPath(const QString &normalizedFolderPath,
                                               bool createDirectory) const
{
    const QString storeDir = localStoreDir(normalizedFolderPath, createDirectory);
    if (storeDir.isEmpty()) {
        return QString();
    }
    return QDir(storeDir).filePath(kMarkJournalFileName);
}

QString ImageMarkManager::localSnapshotPath(const QString &normalizedFolderPath,
                                            bool createDirectory) const
{
    const QString storeDir = localStoreDir(normalizedFolderPath, createDirectory);
    if (storeDir.isEmpty()) {
        return QString();
    }
    return QDir(storeDir).filePath(kMarkFileName);
}

// ---------------------------------------------------------------------------
// Scheduling
// ---------------------------------------------------------------------------

void ImageMarkManager::scheduleJournalWrite(const QString &normalizedFolderPath,
                                            const QString &imageKey,
                                            const QString &category,
                                            qint64 timestamp,
                                            const QString &source,
                                            const QString &reason)
{
    if (!m_worker) {
        return;
    }

    QJsonObject entry;
    entry.insert(kVersionKey, 2);
    entry.insert(kPathKey, imageKey);
    entry.insert(kCategoryKey, category);
    entry.insert(kTimestampKey, timestamp);
    if (!source.isEmpty()) {
        entry.insert(kSourceKey, source);
    }
    if (!reason.isEmpty()) {
        entry.insert(kReasonKey, reason);
    }
    // Frame each record with a LEADING newline as well as a trailing one. A
    // torn append (crash / disk-full mid-write) leaves an unterminated fragment
    // with no trailing '\n'; without a leading newline the *next* record would
    // be read as a continuation of that fragment by readLine() on replay, so
    // both the fragment and the following valid record are lost. The leading
    // newline guarantees every record starts on a fresh line, isolating any
    // torn fragment onto its own (skipped) line. The empty lines this inserts
    // between records are ignored by loadJournalFile().
    QByteArray line;
    line.append('\n');
    line.append(QJsonDocument(entry).toJson(QJsonDocument::Compact));
    line.append('\n');

    // Canonical local durability and remote sidecar replication have independent
    // queues. A remote open/fsync can never block subsequent local marks.
    WriteTask localTask;
    localTask.kind = TaskKind::AppendJournal;
    localTask.primaryPath = localMarkJournalPath(normalizedFolderPath, true);
    localTask.journalLine = line;
    m_worker->enqueue(std::move(localTask));

    if (m_remoteWorker) {
        WriteTask remoteTask;
        remoteTask.kind = TaskKind::AppendJournal;
        remoteTask.primaryPath = markJournalPath(normalizedFolderPath);
        remoteTask.journalLine = std::move(line);
        m_remoteWorker->enqueue(std::move(remoteTask));
    }
}

void ImageMarkManager::scheduleCompaction(const QString &normalizedFolderPath)
{
    if (!m_worker) {
        return;
    }

    QHash<QString, MarkEntry> snapshot;
    {
        QMutexLocker locker(&m_mutex);
        auto it = m_folderMarks.constFind(normalizedFolderPath);
        if (it != m_folderMarks.constEnd()) {
            snapshot = it->marks;
            for (auto pending = it->pendingOverrides.constBegin();
                 pending != it->pendingOverrides.constEnd(); ++pending) {
                snapshot.insert(pending.key(), pending.value());
            }
        }
        m_journalBytesSinceCompaction.insert(normalizedFolderPath, 0);
    }

    WriteTask localTask;
    localTask.kind = TaskKind::CompactSnapshot;
    localTask.snapshot = snapshot;
    localTask.snapshotPath = localSnapshotPath(normalizedFolderPath, true);
    localTask.journalPathToTruncate = localMarkJournalPath(normalizedFolderPath, false);
    m_worker->enqueue(std::move(localTask));

    if (m_remoteWorker) {
        WriteTask remoteTask;
        remoteTask.kind = TaskKind::CompactSnapshot;
        remoteTask.snapshot = std::move(snapshot);
        remoteTask.snapshotPath = markFilePath(normalizedFolderPath);
        remoteTask.journalPathToTruncate = markJournalPath(normalizedFolderPath);
        m_remoteWorker->enqueue(std::move(remoteTask));
    }
}

qint64 ImageMarkManager::nextJournalTimestamp()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 incremented = m_lastJournalTimestamp == (std::numeric_limits<qint64>::max)()
        ? m_lastJournalTimestamp
        : m_lastJournalTimestamp + 1;
    m_lastJournalTimestamp = qMax(now, incremented);
    return m_lastJournalTimestamp;
}
