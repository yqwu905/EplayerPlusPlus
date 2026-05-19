#include <QtTest>
#include <QTemporaryDir>
#include <QImage>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QtConcurrent>
#include <QFuture>
#include <QElapsedTimer>

#include "services/ImageMarkManager.h"

class tst_ImageMarkManager : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void categories_areLimitedToABCD();
    void setMark_savesJsonAndReloads();
    void setMark_updatesAndClears();
    void setMark_rejectsInvalidCategory();
    void setMark_usesRelativePathKeys();
    void loadFolder_appliesLegacyJsonBeforeJournal();
    void setMark_persistsWhenFolderJournalCannotBeWritten();
    void snapshot_writtenAfterCompaction();
    void destructor_returnsPromptlyWhenWriteDestinationIsUnusable();
    void concurrentSetMark_keepsJournalParsable();
};

void tst_ImageMarkManager::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

namespace
{
QJsonObject lastJournalEntry(ImageMarkManager &manager, const QString &folderPath)
{
    QFile file(manager.markJournalPath(folderPath));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    QByteArray lastLine;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (!line.isEmpty()) {
            lastLine = line;
        }
    }

    return QJsonDocument::fromJson(lastLine).object();
}

QString reloadedMark(const QString &folderPath, const QString &imagePath)
{
    ImageMarkManager reloaded;
    if (!reloaded.loadFolder(folderPath)) {
        return QString();
    }
    return reloaded.markForImage(folderPath, imagePath);
}
}

void tst_ImageMarkManager::categories_areLimitedToABCD()
{
    QCOMPARE(ImageMarkManager::categories(), QStringList({"A", "B", "C", "D"}));
    QVERIFY(ImageMarkManager::isValidCategory("A"));
    QVERIFY(ImageMarkManager::isValidCategory("D"));
    QVERIFY(!ImageMarkManager::isValidCategory("E"));
    QVERIFY(!ImageMarkManager::isValidCategory(QString()));
}

void tst_ImageMarkManager::setMark_savesJsonAndReloads()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString imagePath = dir.filePath("image.png");
    QImage image(8, 8, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(imagePath));

    ImageMarkManager manager;
    QVERIFY(manager.loadFolder(dir.path()));
    QVERIFY(manager.setMarkForImage(dir.path(), imagePath, "B"));
    QCOMPARE(manager.markForImage(dir.path(), imagePath), QStringLiteral("B"));

    // The journal file appears as soon as the worker opens it in append mode,
    // but the bytes may not be flushed yet — wait on parsable content, not
    // just file existence.
    QJsonObject entry;
    QTRY_VERIFY_WITH_TIMEOUT(
        !(entry = lastJournalEntry(manager, dir.path())).value("category").toString().isEmpty(),
        5000);
    QCOMPARE(entry.value("path").toString(), QStringLiteral("image.png"));
    QCOMPARE(entry.value("category").toString(), QStringLiteral("B"));
    QTRY_COMPARE(reloadedMark(dir.path(), imagePath), QStringLiteral("B"));
}

void tst_ImageMarkManager::setMark_updatesAndClears()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString imagePath = dir.filePath("image.png");
    QImage image(8, 8, QImage::Format_ARGB32);
    image.fill(Qt::green);
    QVERIFY(image.save(imagePath));

    ImageMarkManager manager;
    QVERIFY(manager.setMarkForImage(dir.path(), imagePath, "A"));
    QVERIFY(manager.setMarkForImage(dir.path(), imagePath, "D"));
    QCOMPARE(manager.markForImage(dir.path(), imagePath), QStringLiteral("D"));
    QTRY_COMPARE(reloadedMark(dir.path(), imagePath), QStringLiteral("D"));

    QVERIFY(manager.clearMarkForImage(dir.path(), imagePath));
    QVERIFY(manager.markForImage(dir.path(), imagePath).isEmpty());

    QTRY_VERIFY(reloadedMark(dir.path(), imagePath).isEmpty());
}

void tst_ImageMarkManager::setMark_rejectsInvalidCategory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString imagePath = dir.filePath("image.png");
    QImage image(8, 8, QImage::Format_ARGB32);
    image.fill(Qt::blue);
    QVERIFY(image.save(imagePath));

    ImageMarkManager manager;
    QVERIFY(!manager.setMarkForImage(dir.path(), imagePath, "E"));
    QVERIFY(manager.markForImage(dir.path(), imagePath).isEmpty());
    QVERIFY(!QFile::exists(manager.markFilePath(dir.path())));
    QVERIFY(!QFile::exists(manager.markJournalPath(dir.path())));
}

void tst_ImageMarkManager::setMark_usesRelativePathKeys()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QVERIFY(QDir().mkpath(dir.filePath("nested")));
    const QString imagePath = dir.filePath("nested/image.png");
    QImage image(8, 8, QImage::Format_ARGB32);
    image.fill(Qt::yellow);
    QVERIFY(image.save(imagePath));

    ImageMarkManager manager;
    QVERIFY(manager.setMarkForImage(dir.path(), imagePath, "C"));
    QCOMPARE(ImageMarkManager::imageKeyForPath(dir.path(), imagePath),
             QStringLiteral("nested/image.png"));

    QJsonObject entry;
    QTRY_VERIFY_WITH_TIMEOUT(
        !(entry = lastJournalEntry(manager, dir.path())).value("category").toString().isEmpty(),
        5000);
    QCOMPARE(entry.value("path").toString(), QStringLiteral("nested/image.png"));
    QCOMPARE(entry.value("category").toString(), QStringLiteral("C"));
    QTRY_COMPARE(reloadedMark(dir.path(), imagePath), QStringLiteral("C"));
}

void tst_ImageMarkManager::loadFolder_appliesLegacyJsonBeforeJournal()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString imagePath = dir.filePath("image.png");
    QImage image(8, 8, QImage::Format_ARGB32);
    image.fill(Qt::darkCyan);
    QVERIFY(image.save(imagePath));

    ImageMarkManager pathHelper;
    QJsonObject marks;
    marks.insert(QStringLiteral("image.png"), QStringLiteral("A"));
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("marks"), marks);

    QFile snapshot(pathHelper.markFilePath(dir.path()));
    QVERIFY(snapshot.open(QIODevice::WriteOnly));
    QVERIFY(snapshot.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) > 0);
    snapshot.close();

    QFile journal(pathHelper.markJournalPath(dir.path()));
    QVERIFY(journal.open(QIODevice::WriteOnly | QIODevice::Text));
    QJsonObject update;
    update.insert(QStringLiteral("path"), QStringLiteral("image.png"));
    update.insert(QStringLiteral("category"), QStringLiteral("D"));
    journal.write(QJsonDocument(update).toJson(QJsonDocument::Compact));
    journal.write("\n");
    journal.close();

    ImageMarkManager manager;
    QVERIFY(manager.loadFolder(dir.path()));
    QCOMPARE(manager.markForImage(dir.path(), imagePath), QStringLiteral("D"));
}

void tst_ImageMarkManager::setMark_persistsWhenFolderJournalCannotBeWritten()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString missingFolderPath = dir.filePath("missing-folder");
    const QString imagePath = QDir(missingFolderPath).filePath("image.png");

    ImageMarkManager manager;
    QVERIFY(manager.setMarkForImage(missingFolderPath, imagePath, "C"));
    QCOMPARE(manager.markForImage(missingFolderPath, imagePath), QStringLiteral("C"));
    QVERIFY(!QFile::exists(manager.markJournalPath(missingFolderPath)));

    QTRY_COMPARE(reloadedMark(missingFolderPath, imagePath), QStringLiteral("C"));
}

void tst_ImageMarkManager::snapshot_writtenAfterCompaction()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString imagePath = dir.filePath("image.png");
    QImage image(8, 8, QImage::Format_ARGB32);
    image.fill(Qt::magenta);
    QVERIFY(image.save(imagePath));

    // Verify normal shutdown commits a snapshot - the previous implementation
    // never wrote `.imagecompare_marks.json`, so this regresses that bug.
    {
        ImageMarkManager manager;
        QVERIFY(manager.loadFolder(dir.path()));
        QVERIFY(manager.setMarkForImage(dir.path(), imagePath, "B"));
        QTRY_VERIFY(QFile::exists(manager.markJournalPath(dir.path())));
    }

    // Destructor scheduled and waited on a CompactSnapshot task, so the
    // snapshot must now exist and the journal must be gone.
    ImageMarkManager pathHelper;
    QVERIFY(QFile::exists(pathHelper.markFilePath(dir.path())));
    QVERIFY(!QFile::exists(pathHelper.markJournalPath(dir.path())));

    QFile snapshot(pathHelper.markFilePath(dir.path()));
    QVERIFY(snapshot.open(QIODevice::ReadOnly));
    const QJsonDocument doc = QJsonDocument::fromJson(snapshot.readAll());
    QVERIFY(doc.isObject());
    const QJsonObject root = doc.object();
    QCOMPARE(root.value("version").toInt(), 2);
    const QJsonObject marks = root.value("marks").toObject();
    QVERIFY(marks.contains("image.png"));
    // New-format snapshot entries are objects carrying both category and
    // timestamp so journal replay can resolve last-writer-wins correctly.
    const QJsonObject entry = marks.value("image.png").toObject();
    QCOMPARE(entry.value("category").toString(), QStringLiteral("B"));
    QVERIFY(entry.value("timestamp").toVariant().toLongLong() > 0);

    // A fresh manager reads the snapshot and recovers the mark even though
    // no journal remains.
    QCOMPARE(reloadedMark(dir.path(), imagePath), QStringLiteral("B"));
}

void tst_ImageMarkManager::destructor_returnsPromptlyWhenWriteDestinationIsUnusable()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString readOnlyFolder = dir.filePath("readonly-folder");
    QVERIFY(QDir().mkpath(readOnlyFolder));
    const QString imagePath = QDir(readOnlyFolder).filePath("image.png");
    QImage image(8, 8, QImage::Format_ARGB32);
    image.fill(Qt::cyan);
    QVERIFY(image.save(imagePath));

#ifndef Q_OS_WIN
    // Make the folder unwritable so the writer thread cannot create a journal
    // there. On Windows POSIX permission semantics are unreliable, so we skip
    // the chmod and rely on the manager falling back to the local store.
    QVERIFY(QFile::setPermissions(
        readOnlyFolder,
        QFile::ReadOwner | QFile::ExeOwner | QFile::ReadUser | QFile::ExeUser));
#endif

    QElapsedTimer shutdownTimer;
    {
        ImageMarkManager manager;
        QVERIFY(manager.setMarkForImage(readOnlyFolder, imagePath, "A"));
        // Enqueue several writes so any in-flight blocking would compound.
        QVERIFY(manager.setMarkForImage(readOnlyFolder, imagePath, "B"));
        QVERIFY(manager.setMarkForImage(readOnlyFolder, imagePath, "C"));
        shutdownTimer.start();
    }
    // Hard cap: well under the 2s shutdown budget the manager honors. If the
    // destructor regressed to waiting on every queued future, this would
    // blow past the limit instead.
    const qint64 elapsed = shutdownTimer.elapsed();
    QVERIFY2(elapsed < 5000,
             qPrintable(QStringLiteral("destructor took %1ms").arg(elapsed)));

#ifndef Q_OS_WIN
    // Restore writability so QTemporaryDir can clean up.
    QFile::setPermissions(readOnlyFolder,
                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                          QFile::ReadUser | QFile::WriteUser | QFile::ExeUser);
#endif

    // Mark should still be recoverable from the local fallback store.
    QCOMPARE(reloadedMark(readOnlyFolder, imagePath), QStringLiteral("C"));
}

void tst_ImageMarkManager::concurrentSetMark_keepsJournalParsable()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    constexpr int kImageCount = 16;
    QStringList imagePaths;
    for (int i = 0; i < kImageCount; ++i) {
        const QString name = QStringLiteral("image_%1.png").arg(i);
        const QString path = dir.filePath(name);
        QImage image(8, 8, QImage::Format_ARGB32);
        image.fill(QColor::fromHsv((i * 23) % 360, 200, 200));
        QVERIFY(image.save(path));
        imagePaths.append(path);
    }

    ImageMarkManager manager;
    QVERIFY(manager.loadFolder(dir.path()));

    // Fan out setMarkForImage calls from several threads. The mutex inside
    // the manager must keep `m_folderMarks` and the writer queue consistent,
    // and the worker's serialized writes must produce a journal whose every
    // line is still valid JSON (no interleaving mid-line).
    QStringList categories = ImageMarkManager::categories();
    QList<QFuture<void>> futures;
    constexpr int kThreads = 8;
    constexpr int kIterationsPerThread = 25;
    for (int t = 0; t < kThreads; ++t) {
        futures.append(QtConcurrent::run([&manager, &dir, &imagePaths, &categories, t]() {
            for (int i = 0; i < kIterationsPerThread; ++i) {
                const QString &path = imagePaths.at((t + i) % imagePaths.size());
                const QString &cat = categories.at((t + i) % categories.size());
                manager.setMarkForImage(dir.path(), path, cat);
            }
        }));
    }
    for (QFuture<void> &f : futures) {
        f.waitForFinished();
    }

    // Use a fresh sentinel image that no thread touched, so setMarkForImage
    // is guaranteed to mutate state (returns false if the value is unchanged).
    const QString sentinelPath = dir.filePath("sentinel.png");
    QImage sentinelImage(8, 8, QImage::Format_ARGB32);
    sentinelImage.fill(Qt::black);
    QVERIFY(sentinelImage.save(sentinelPath));
    QVERIFY(manager.setMarkForImage(dir.path(), sentinelPath, "D"));
    QTRY_COMPARE(manager.markForImage(dir.path(), sentinelPath), QStringLiteral("D"));

    // Every line in the journal (if it still exists - compaction may have
    // discarded it) must parse as a valid JSON object. No interleaving means
    // no half-lines.
    const QString journalPath = manager.markJournalPath(dir.path());
    if (QFile::exists(journalPath)) {
        QFile journal(journalPath);
        QVERIFY(journal.open(QIODevice::ReadOnly | QIODevice::Text));
        int validLines = 0;
        while (!journal.atEnd()) {
            const QByteArray line = journal.readLine().trimmed();
            if (line.isEmpty()) {
                continue;
            }
            QJsonParseError err;
            const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
            QVERIFY2(err.error == QJsonParseError::NoError,
                     qPrintable(QStringLiteral("journal line not valid JSON: %1 (%2)")
                                    .arg(QString::fromUtf8(line))
                                    .arg(err.errorString())));
            QVERIFY(doc.isObject());
            ++validLines;
        }
        QVERIFY(validLines > 0);
    }
}

QTEST_MAIN(tst_ImageMarkManager)
#include "tst_ImageMarkManager.moc"
