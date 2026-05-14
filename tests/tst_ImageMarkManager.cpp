#include <QtTest>
#include <QTemporaryDir>
#include <QImage>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

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

    QTRY_VERIFY(QFile::exists(manager.markJournalPath(dir.path())));
    const QJsonObject entry = lastJournalEntry(manager, dir.path());
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

    QTRY_VERIFY(QFile::exists(manager.markJournalPath(dir.path())));
    const QJsonObject entry = lastJournalEntry(manager, dir.path());
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

QTEST_MAIN(tst_ImageMarkManager)
#include "tst_ImageMarkManager.moc"
