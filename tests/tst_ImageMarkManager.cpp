#include <QtTest>
#include <QTemporaryDir>
#include <QImage>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>

#include "services/ImageMarkManager.h"

class tst_ImageMarkManager : public QObject
{
    Q_OBJECT

private slots:
    void categories_areLimitedToABCD();
    void setMark_savesJsonAndReloads();
    void setMark_updatesAndClears();
    void setMark_rejectsInvalidCategory();
    void setMark_usesRelativePathKeys();
};

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

    QFile file(manager.markFilePath(dir.path()));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    QCOMPARE(root.value("marks").toObject().value("image.png").toString(),
             QStringLiteral("B"));

    ImageMarkManager reloaded;
    QVERIFY(reloaded.loadFolder(dir.path()));
    QCOMPARE(reloaded.markForImage(dir.path(), imagePath), QStringLiteral("B"));
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

    QVERIFY(manager.clearMarkForImage(dir.path(), imagePath));
    QVERIFY(manager.markForImage(dir.path(), imagePath).isEmpty());

    QFile file(manager.markFilePath(dir.path()));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonObject marks = QJsonDocument::fromJson(file.readAll())
                                  .object()
                                  .value("marks")
                                  .toObject();
    QVERIFY(marks.isEmpty());
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

    QFile file(manager.markFilePath(dir.path()));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonObject marks = QJsonDocument::fromJson(file.readAll())
                                  .object()
                                  .value("marks")
                                  .toObject();
    QCOMPARE(marks.value("nested/image.png").toString(), QStringLiteral("C"));
}

QTEST_MAIN(tst_ImageMarkManager)
#include "tst_ImageMarkManager.moc"
