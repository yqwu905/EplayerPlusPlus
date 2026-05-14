#include "ImageMarkManager.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QThread>
#include <QtConcurrent>

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
constexpr int kSharedJournalWriteTimeoutMs = 250;
constexpr int kSharedJournalPollIntervalMs = 5;

bool appendNormalizedJournalEntry(const QString &journalPath,
                                  const QString &normalizedImageKey,
                                  const QString &category,
                                  qint64 timestamp)
{
    if (journalPath.isEmpty() || normalizedImageKey.isEmpty()) {
        return false;
    }

    QJsonObject entry;
    entry.insert(kVersionKey, 2);
    entry.insert(kPathKey, normalizedImageKey);
    entry.insert(kCategoryKey, category);
    entry.insert(kTimestampKey, timestamp);

    QByteArray line = QJsonDocument(entry).toJson(QJsonDocument::Compact);
    line.append('\n');

    QFile file(journalPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return false;
    }

    return file.write(line) == line.size() && file.flush();
}

void persistJournalEntryWithFallback(const QString &sharedJournalPath,
                                     const QString &localJournalPath,
                                     const QString &normalizedImageKey,
                                     const QString &category,
                                     qint64 timestamp)
{
    QFuture<bool> sharedWrite = QtConcurrent::run([sharedJournalPath,
                                                   normalizedImageKey,
                                                   category,
                                                   timestamp]() {
        return appendNormalizedJournalEntry(sharedJournalPath,
                                            normalizedImageKey,
                                            category,
                                            timestamp);
    });

    QElapsedTimer timer;
    timer.start();
    while (!sharedWrite.isFinished() && timer.elapsed() < kSharedJournalWriteTimeoutMs) {
        QThread::msleep(kSharedJournalPollIntervalMs);
    }

    if (sharedWrite.isFinished() && sharedWrite.result()) {
        return;
    }

    appendNormalizedJournalEntry(localJournalPath, normalizedImageKey, category, timestamp);
}
}

ImageMarkManager::ImageMarkManager(QObject *parent)
    : QObject(parent)
{
}

ImageMarkManager::~ImageMarkManager()
{
    for (QFuture<void> &future : m_pendingWrites) {
        future.waitForFinished();
    }
}

QStringList ImageMarkManager::categories()
{
    return {
        QStringLiteral("A"),
        QStringLiteral("B"),
        QStringLiteral("C"),
        QStringLiteral("D")
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

bool ImageMarkManager::loadFolder(const QString &folderPath)
{
    const QString normalizedFolder = normalizeFolderPath(folderPath);
    if (normalizedFolder.isEmpty()) {
        return false;
    }

    FolderMarks &folderMarks = m_folderMarks[normalizedFolder];
    if (folderMarks.loaded) {
        return true;
    }

    folderMarks.loaded = true;
    folderMarks.marks.clear();

    loadSnapshot(normalizedFolder, folderMarks);
    loadJournal(normalizedFolder, folderMarks);

    return true;
}

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
    FolderMarks &folderMarks = m_folderMarks[normalizedFolder];
    const auto previousIt = folderMarks.marks.constFind(key);
    const QString previous = previousIt == folderMarks.marks.constEnd()
        ? QString()
        : previousIt->category;
    const qint64 timestamp = nextJournalTimestamp();

    if (category.isEmpty()) {
        if (previousIt == folderMarks.marks.constEnd() || previous.isEmpty()) {
            return false;
        }
        folderMarks.marks.insert(key, MarkEntry{QString(), timestamp});
    } else {
        if (previous == category) {
            return false;
        }
        folderMarks.marks.insert(key, MarkEntry{category, timestamp});
    }

    scheduleJournalWrite(normalizedFolder, key, category, timestamp);
    emit markChanged(normalizedFolder, QDir(normalizedFolder).filePath(key), category);
    return true;
}

bool ImageMarkManager::clearMarkForImage(const QString &folderPath,
                                         const QString &imagePath)
{
    return setMarkForImage(folderPath, imagePath, QString());
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

bool ImageMarkManager::loadSnapshot(const QString &normalizedFolderPath,
                                    FolderMarks &folderMarks) const
{
    QFile file(markFilePath(normalizedFolderPath));
    if (!file.exists()) {
        return true;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        return false;
    }

    const QJsonObject root = document.object();
    const QJsonObject marks = root.value(kMarksKey).toObject();
    for (auto it = marks.constBegin(); it != marks.constEnd(); ++it) {
        applyStoredMark(folderMarks,
                        it.key(),
                        it.value().toString(),
                        kUntimedJournalEntryTimestamp);
    }

    return true;
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

QString ImageMarkManager::localMarkJournalPath(const QString &normalizedFolderPath,
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

    QString fallbackPath;
    for (const QString &root : roots) {
        const QString storeDir = QDir(root).filePath(
            QStringLiteral("image-marks/%1").arg(QString::fromLatin1(folderHash)));
        const QString journalPath = QDir(storeDir).filePath(kMarkJournalFileName);
        if (!createDirectory) {
            if (QFileInfo::exists(journalPath)) {
                return journalPath;
            }
            if (fallbackPath.isEmpty()) {
                fallbackPath = journalPath;
            }
            continue;
        }
        if (QDir().mkpath(storeDir)) {
            return journalPath;
        }
    }

    return fallbackPath;
}

void ImageMarkManager::scheduleJournalWrite(const QString &normalizedFolderPath,
                                            const QString &imageKey,
                                            const QString &category,
                                            qint64 timestamp)
{
    pruneFinishedWrites();

    const QString sharedJournalPath = markJournalPath(normalizedFolderPath);
    const QString localJournalPath = localMarkJournalPath(normalizedFolderPath, true);
    const QString normalizedKey = normalizeStoredKey(imageKey);

    m_pendingWrites.append(QtConcurrent::run([sharedJournalPath,
                                              localJournalPath,
                                              normalizedKey,
                                              category,
                                              timestamp]() {
        persistJournalEntryWithFallback(sharedJournalPath,
                                        localJournalPath,
                                        normalizedKey,
                                        category,
                                        timestamp);
    }));
}

qint64 ImageMarkManager::nextJournalTimestamp()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_lastJournalTimestamp = qMax(now, m_lastJournalTimestamp + 1);
    return m_lastJournalTimestamp;
}

void ImageMarkManager::pruneFinishedWrites()
{
    for (int i = m_pendingWrites.size() - 1; i >= 0; --i) {
        if (m_pendingWrites.at(i).isFinished()) {
            m_pendingWrites.removeAt(i);
        }
    }
}
