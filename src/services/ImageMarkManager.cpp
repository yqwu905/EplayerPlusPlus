#include "ImageMarkManager.h"

#include <QCryptographicHash>
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
#include <QTemporaryFile>

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
constexpr qint64 kUntimedJournalEntryTimestamp = 0;

// Threshold at which an append triggers snapshot compaction. 64 KiB keeps
// load-time replay cheap while not thrashing the disk on every mark change.
constexpr qint64 kJournalCompactBytes = 64 * 1024;
// Hard cap on how long the destructor waits for the writer to drain. If the
// shared journal lives on a stuck network share the local fallback already
// has the data, so abandoning is safe.
constexpr int kShutdownWaitMs = 2000;
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
    const QStringList attempts = { task.primaryPath, task.fallbackPath };
    for (const QString &path : attempts) {
        if (path.isEmpty()) {
            continue;
        }
        if (writeJournalLine(path, task.journalLine)) {
            return true;
        }
    }
    qCWarning(lcMarkManager).noquote()
        << "ImageMarkManager: failed to append journal entry to"
        << task.primaryPath << "(fallback:" << task.fallbackPath << ")";
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
        qCWarning(lcMarkManager).noquote()
            << "ImageMarkManager: snapshot write failed for"
            << task.snapshotPath;
        return false;
    }

    // Snapshot is committed; the journal is now redundant.
    if (!task.journalPathToTruncate.isEmpty()) {
        QFile::remove(task.journalPathToTruncate);
    }
    if (!task.fallbackJournalPathToTruncate.isEmpty()) {
        QFile::remove(task.fallbackJournalPathToTruncate);
    }
    if (!task.fallbackSnapshotPathToTruncate.isEmpty()) {
        QFile::remove(task.fallbackSnapshotPathToTruncate);
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
    m_worker->start();
}

ImageMarkManager::~ImageMarkManager()
{
    if (!m_worker) {
        return;
    }

    // Snapshot-compact only folders that THIS manager actually wrote to
    // (m_journalBytesSinceCompaction > 0). A read-only manager that just
    // called loadFolder must not write a snapshot — doing so would race
    // with a concurrent writer's pending journal entries, and the writer's
    // subsequent compaction unlinks the journal, losing the data.
    QList<QString> foldersToCompact;
    {
        QMutexLocker locker(&m_mutex);
        foldersToCompact.reserve(m_journalBytesSinceCompaction.size());
        for (auto it = m_journalBytesSinceCompaction.constBegin();
             it != m_journalBytesSinceCompaction.constEnd(); ++it) {
            if (it.value() <= 0) {
                continue;
            }
            auto folderIt = m_folderMarks.constFind(it.key());
            if (folderIt != m_folderMarks.constEnd() && folderIt->loaded) {
                foldersToCompact.append(it.key());
            }
        }
    }
    for (const QString &folder : foldersToCompact) {
        scheduleCompaction(folder);
    }

    m_worker->requestShutdown();
    if (!m_worker->wait(kShutdownWaitMs)) {
        qCWarning(lcMarkManager).noquote()
            << "ImageMarkManager: background writer did not finish within"
            << kShutdownWaitMs
            << "ms; abandoning thread. Marks remain in the local fallback"
               " store and will be picked up on next start.";
        // Leak the thread on purpose: deleting a still-running QThread would
        // qFatal. The worker is self-contained (no back-pointer into us) and
        // it will be torn down by the OS at process exit. The cost is one
        // pinned thread for the rest of the process lifetime.
        m_worker = nullptr;
        return;
    }
    delete m_worker;
    m_worker = nullptr;
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
                                       qint64 timestamp)
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
        folderMarks.marks.insert(key, MarkEntry{QString(), timestamp});
    } else if (isValidCategory(category)) {
        folderMarks.marks.insert(key, MarkEntry{category, timestamp});
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
    slot.loaded = true;
    slot.marks.clear();
    loadSnapshot(normalizedFolder, slot);
    loadJournal(normalizedFolder, slot);
    return true;
}

bool ImageMarkManager::loadSnapshot(const QString &normalizedFolderPath,
                                    FolderMarks &folderMarks) const
{
    // Try the folder snapshot first, then fall back to the local store so
    // marks written in fallback mode survive across runs even if the source
    // folder remains read-only.
    const QStringList candidates = {
        markFilePath(normalizedFolderPath),
        localSnapshotPath(normalizedFolderPath, false)
    };

    bool readAny = false;
    for (const QString &path : candidates) {
        if (path.isEmpty()) {
            continue;
        }
        QFile file(path);
        if (!file.exists()) {
            continue;
        }
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        if (!document.isObject()) {
            continue;
        }
        const QJsonObject root = document.object();
        const QJsonObject marks = root.value(kMarksKey).toObject();
        for (auto it = marks.constBegin(); it != marks.constEnd(); ++it) {
            const QJsonValue value = it.value();
            // New-format snapshot stores {category, timestamp} per entry; old
            // format (v1) stored just the category string. Accept both.
            if (value.isObject()) {
                const QJsonObject entry = value.toObject();
                applyStoredMark(folderMarks,
                                it.key(),
                                entry.value(kCategoryKey).toString(),
                                entry.value(kTimestampKey).toVariant().toLongLong());
            } else {
                applyStoredMark(folderMarks,
                                it.key(),
                                value.toString(),
                                kUntimedJournalEntryTimestamp);
            }
        }
        readAny = true;
    }
    return readAny;
}

bool ImageMarkManager::loadJournal(const QString &normalizedFolderPath,
                                   FolderMarks &folderMarks) const
{
    loadJournalFile(markJournalPath(normalizedFolderPath), folderMarks);
    return loadJournalFile(localMarkJournalPath(normalizedFolderPath, false), folderMarks);
}

bool ImageMarkManager::loadJournalFile(const QString &journalPath,
                                       FolderMarks &folderMarks) const
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
                        entry.value(kTimestampKey).toVariant().toLongLong());
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
    if (folderIt == m_folderMarks.constEnd() || !folderIt->loaded) {
        return QString();
    }

    const auto markIt = folderIt->marks.constFind(key);
    if (markIt == folderIt->marks.constEnd()) {
        return QString();
    }

    return markIt->category;
}

QHash<QString, QString> ImageMarkManager::marksForFolder(const QString &folderPath) const
{
    const QString normalizedFolder = normalizeFolderPath(folderPath);
    if (normalizedFolder.isEmpty()) {
        return {};
    }

    QMutexLocker locker(&m_mutex);
    auto folderIt = m_folderMarks.constFind(normalizedFolder);
    if (folderIt == m_folderMarks.constEnd() || !folderIt->loaded) {
        return {};
    }

    QHash<QString, QString> result;
    for (auto it = folderIt->marks.constBegin(); it != folderIt->marks.constEnd(); ++it) {
        if (!it->category.isEmpty()) {
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
    if (!category.isEmpty() && !isValidCategory(category)) {
        return false;
    }

    const QString normalizedFolder = normalizeFolderPath(folderPath);
    const QString key = normalizeStoredKey(imageKey);
    if (normalizedFolder.isEmpty() || key.isEmpty()) {
        return false;
    }

    loadFolder(normalizedFolder);

    qint64 timestamp = 0;
    qint64 estimatedBytes = 0;
    bool shouldCompact = false;
    {
        QMutexLocker locker(&m_mutex);
        FolderMarks &folderMarks = m_folderMarks[normalizedFolder];
        const auto previousIt = folderMarks.marks.constFind(key);
        const QString previous = previousIt == folderMarks.marks.constEnd()
            ? QString()
            : previousIt->category;

        if (category.isEmpty()) {
            if (previousIt == folderMarks.marks.constEnd() || previous.isEmpty()) {
                return false;
            }
        } else if (previous == category) {
            return false;
        }

        timestamp = nextJournalTimestamp();
        if (category.isEmpty()) {
            folderMarks.marks.insert(key, MarkEntry{QString(), timestamp});
        } else {
            folderMarks.marks.insert(key, MarkEntry{category, timestamp});
        }

        // Cheap upper-bound estimate of how much we are about to write. The
        // worker will write the same shape so this is accurate enough for
        // compaction triggering.
        estimatedBytes = static_cast<qint64>(key.size())
                         + static_cast<qint64>(category.size())
                         + 64; // JSON overhead + timestamp + newline.
        qint64 &accum = m_journalBytesSinceCompaction[normalizedFolder];
        accum += estimatedBytes;
        if (accum >= kJournalCompactBytes) {
            shouldCompact = true;
            accum = 0;
        }
    }

    scheduleJournalWrite(normalizedFolder, key, category, timestamp);
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

// ---------------------------------------------------------------------------
// Store-target selection
// ---------------------------------------------------------------------------

ImageMarkManager::StoreTarget ImageMarkManager::chooseStoreTarget(
    const QString &normalizedFolderPath)
{
    {
        QMutexLocker locker(&m_mutex);
        auto it = m_storeTargetCache.constFind(normalizedFolderPath);
        if (it != m_storeTargetCache.constEnd()) {
            return *it;
        }
    }

    // Probe writability by creating and immediately removing a temporary file
    // inside the folder. Touching the journal file itself could spuriously
    // create an empty journal on a folder that should be local-only.
    StoreTarget chosen = StoreTarget::Local;
    QDir folderDir(normalizedFolderPath);
    if (folderDir.exists()) {
        QTemporaryFile probe(folderDir.filePath(QStringLiteral(".imagecompare_probe_XXXXXX")));
        probe.setAutoRemove(true);
        if (probe.open()) {
            chosen = StoreTarget::Folder;
        }
    }

    {
        QMutexLocker locker(&m_mutex);
        m_storeTargetCache.insert(normalizedFolderPath, chosen);
    }
    return chosen;
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

QString ImageMarkManager::journalPathForStore(const QString &normalizedFolderPath,
                                              StoreTarget target) const
{
    return target == StoreTarget::Folder
               ? markJournalPath(normalizedFolderPath)
               : localMarkJournalPath(normalizedFolderPath, true);
}

QString ImageMarkManager::snapshotPathForStore(const QString &normalizedFolderPath,
                                               StoreTarget target) const
{
    return target == StoreTarget::Folder
               ? markFilePath(normalizedFolderPath)
               : localSnapshotPath(normalizedFolderPath, true);
}

// ---------------------------------------------------------------------------
// Scheduling
// ---------------------------------------------------------------------------

void ImageMarkManager::scheduleJournalWrite(const QString &normalizedFolderPath,
                                            const QString &imageKey,
                                            const QString &category,
                                            qint64 timestamp)
{
    if (!m_worker) {
        return;
    }

    const StoreTarget primary = chooseStoreTarget(normalizedFolderPath);

    QJsonObject entry;
    entry.insert(kVersionKey, 2);
    entry.insert(kPathKey, imageKey);
    entry.insert(kCategoryKey, category);
    entry.insert(kTimestampKey, timestamp);
    QByteArray line = QJsonDocument(entry).toJson(QJsonDocument::Compact);
    line.append('\n');

    WriteTask task;
    task.kind = TaskKind::AppendJournal;
    task.primaryPath = journalPathForStore(normalizedFolderPath, primary);
    if (primary == StoreTarget::Folder) {
        // Local fallback so that if the folder write fails (e.g. it became
        // read-only between probe and write) the mark is still durable.
        task.fallbackPath = localMarkJournalPath(normalizedFolderPath, true);
    }
    task.journalLine = std::move(line);
    m_worker->enqueue(std::move(task));
}

void ImageMarkManager::scheduleCompaction(const QString &normalizedFolderPath)
{
    if (!m_worker) {
        return;
    }

    const StoreTarget primary = chooseStoreTarget(normalizedFolderPath);

    WriteTask task;
    task.kind = TaskKind::CompactSnapshot;
    task.snapshotPath = snapshotPathForStore(normalizedFolderPath, primary);
    task.journalPathToTruncate = journalPathForStore(normalizedFolderPath, primary);
    if (primary == StoreTarget::Folder) {
        // Once the folder snapshot is committed, prune any stale local
        // fallback files so the next replay does not resurrect them.
        task.fallbackJournalPathToTruncate = localMarkJournalPath(normalizedFolderPath, false);
        task.fallbackSnapshotPathToTruncate = localSnapshotPath(normalizedFolderPath, false);
    }
    {
        QMutexLocker locker(&m_mutex);
        auto it = m_folderMarks.constFind(normalizedFolderPath);
        if (it != m_folderMarks.constEnd()) {
            task.snapshot = it->marks;
        }
        m_journalBytesSinceCompaction.insert(normalizedFolderPath, 0);
    }
    m_worker->enqueue(std::move(task));
}

qint64 ImageMarkManager::nextJournalTimestamp()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_lastJournalTimestamp = qMax(now, m_lastJournalTimestamp + 1);
    return m_lastJournalTimestamp;
}
