#include <QTest>
#include <QTemporaryDir>
#include <QImage>
#include <QSet>
#include <QPushButton>
#include <QDir>
#include <QCoreApplication>

#include "models/CompareSession.h"
#include "widgets/ComparePanel.h"

class tst_ComparePanel : public QObject
{
    Q_OBJECT

private slots:
    void layout_oneToThreeImages_singleRow();
    void layout_fourToSixImages_twoRows();
    void compareButtons_nMinusOnePerImage();

private:
    static QString createImageInFolder(const QString &folderPath, const QString &name, const QColor &color);
    static QList<QWidget *> findCells(ComparePanel &panel);
};

QString tst_ComparePanel::createImageInFolder(const QString &folderPath, const QString &name, const QColor &color)
{
    QImage image(32, 32, QImage::Format_ARGB32);
    image.fill(color);
    const QString imagePath = folderPath + "/" + name;
    if (!image.save(imagePath)) {
        return QString();
    }
    return imagePath;
}

QList<QWidget *> tst_ComparePanel::findCells(ComparePanel &panel)
{
    return panel.findChildren<QWidget *>(QStringLiteral("compareCellContainer"));
}

void tst_ComparePanel::layout_oneToThreeImages_singleRow()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    CompareSession session;
    ComparePanel panel(&session, nullptr);
    panel.resize(1200, 800);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    QList<QPair<QString, QString>> selected;
    for (int i = 0; i < 3; ++i) {
        const QString folder = root.filePath(QString("folder_%1").arg(i));
        QVERIFY(QDir().mkpath(folder));
        const QString imagePath = createImageInFolder(folder, "img.png", QColor::fromHsv((i * 60) % 360, 255, 255));
        QVERIFY(!imagePath.isEmpty());
        QVERIFY(session.addFolder(folder));
        selected.append({folder, imagePath});
    }
    panel.setSelectedImages(selected);
    QCoreApplication::processEvents();

    const auto cells = findCells(panel);
    QCOMPARE(cells.size(), 3);

    QSet<int> uniqueRows;
    for (QWidget *cell : cells) {
        uniqueRows.insert(cell->mapTo(&panel, QPoint(0, 0)).y() / 20);
    }
    QCOMPARE(uniqueRows.size(), 1);
}

void tst_ComparePanel::layout_fourToSixImages_twoRows()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    CompareSession session;
    ComparePanel panel(&session, nullptr);
    panel.resize(1400, 900);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    QList<QPair<QString, QString>> selected;
    for (int i = 0; i < 6; ++i) {
        const QString folder = root.filePath(QString("folder_%1").arg(i));
        QVERIFY(QDir().mkpath(folder));
        const QString imagePath = createImageInFolder(folder, "img.png", QColor::fromHsv((i * 40) % 360, 255, 255));
        QVERIFY(!imagePath.isEmpty());
        QVERIFY(session.addFolder(folder));
        selected.append({folder, imagePath});
    }
    panel.setSelectedImages(selected);
    QCoreApplication::processEvents();

    const auto cells = findCells(panel);
    QCOMPARE(cells.size(), 6);

    QSet<int> uniqueRows;
    for (QWidget *cell : cells) {
        uniqueRows.insert(cell->mapTo(&panel, QPoint(0, 0)).y() / 20);
    }
    QCOMPARE(uniqueRows.size(), 2);
}

void tst_ComparePanel::compareButtons_nMinusOnePerImage()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    CompareSession session;
    ComparePanel panel(&session, nullptr);
    panel.resize(1400, 900);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    QList<QPair<QString, QString>> selected;
    for (int i = 0; i < 6; ++i) {
        const QString folder = root.filePath(QString("folder_%1").arg(i));
        QVERIFY(QDir().mkpath(folder));
        const QString imagePath = createImageInFolder(folder, "img.png", QColor::fromHsv((i * 30) % 360, 255, 255));
        QVERIFY(!imagePath.isEmpty());
        QVERIFY(session.addFolder(folder));
        selected.append({folder, imagePath});
    }
    panel.setSelectedImages(selected);
    QCoreApplication::processEvents();

    const auto cells = findCells(panel);
    QCOMPARE(cells.size(), 6);

    for (QWidget *cell : cells) {
        const auto buttons = cell->findChildren<QPushButton *>();
        int compareButtonCount = 0;
        for (QPushButton *button : buttons) {
            if (button->text().startsWith(QStringLiteral("对比 "))) {
                ++compareButtonCount;
            }
        }
        QCOMPARE(compareButtonCount, 5);
    }
}

QTEST_MAIN(tst_ComparePanel)
#include "tst_ComparePanel.moc"
