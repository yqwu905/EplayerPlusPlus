#include <QTest>
#include <QTemporaryDir>
#include <QImage>
#include <QSet>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QDir>
#include <QCoreApplication>
#include <algorithm>

#include "models/CompareSession.h"
#include "services/ImageLoader.h"
#include "services/ImageMarkManager.h"
#include "widgets/ComparePanel.h"
#include "widgets/ZoomableImageWidget.h"

class tst_ComparePanel : public QObject
{
    Q_OBJECT

private slots:
    void layout_oneToThreeImages_singleRow();
    void layout_fourToSixImages_twoRows();
    void compareButtons_nMinusOnePerImage();
    void compareHeader_placesTitleAndButtonsOnSameRow();
    void layout_shrinksFromSixToTwo_cellsExpand();
    void resizeToFirstImage_toggleResizesOtherCells();
    void markButton_clickPersistsSingleImage();
    void markButton_ctrlClickMarksAllCurrentImages();
    void markShortcut_marksAllCurrentImages();

private:
    static QString createImageInFolder(const QString &folderPath, const QString &name, const QColor &color);
    static QList<QWidget *> findCells(ComparePanel &panel);
    static QList<QWidget *> sortedCells(ComparePanel &panel);
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

QList<QWidget *> tst_ComparePanel::sortedCells(ComparePanel &panel)
{
    auto cells = findCells(panel);
    std::sort(cells.begin(), cells.end(), [&panel](QWidget *lhs, QWidget *rhs) {
        const QPoint lhsPos = lhs->mapTo(&panel, QPoint(0, 0));
        const QPoint rhsPos = rhs->mapTo(&panel, QPoint(0, 0));
        if (lhsPos.y() / 20 != rhsPos.y() / 20) {
            return lhsPos.y() < rhsPos.y();
        }
        return lhsPos.x() < rhsPos.x();
    });
    return cells;
}

void tst_ComparePanel::layout_oneToThreeImages_singleRow()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    CompareSession session;
    ImageLoader loader;
    ComparePanel panel(&session, nullptr, &loader);
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
    ImageLoader loader;
    ComparePanel panel(&session, nullptr, &loader);
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
    ImageLoader loader;
    ComparePanel panel(&session, nullptr, &loader);
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
        const auto buttons = cell->findChildren<QPushButton *>(
            QStringLiteral("compareTargetButton"));
        int compareButtonCount = 0;
        for (QPushButton *button : buttons) {
            bool isNumber = false;
            const int targetNumber = button->text().toInt(&isNumber);
            if (isNumber && targetNumber >= 1 && targetNumber <= 6) {
                QVERIFY(!button->text().contains(QStringLiteral("对比")));
                ++compareButtonCount;
            }
        }
        QCOMPARE(compareButtonCount, 5);
    }
}

void tst_ComparePanel::compareHeader_placesTitleAndButtonsOnSameRow()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    CompareSession session;
    ImageLoader loader;
    ComparePanel panel(&session, nullptr, &loader);
    panel.resize(1000, 700);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    QList<QPair<QString, QString>> selected;
    for (int i = 0; i < 3; ++i) {
        const QString folder = root.filePath(QString("folder_%1").arg(i));
        QVERIFY(QDir().mkpath(folder));
        const QString imagePath = createImageInFolder(
            folder,
            QString("image_%1.png").arg(i),
            QColor::fromHsv((i * 50) % 360, 255, 255));
        QVERIFY(!imagePath.isEmpty());
        QVERIFY(session.addFolder(folder));
        selected.append({folder, imagePath});
    }
    panel.setSelectedImages(selected);
    QCoreApplication::processEvents();

    const auto cells = findCells(panel);
    QCOMPARE(cells.size(), 3);

    for (QWidget *cell : cells) {
        auto *title = cell->findChild<QLabel *>(QStringLiteral("compareCellHeaderLabel"));
        QVERIFY(title != nullptr);

        const auto buttons = cell->findChildren<QPushButton *>(
            QStringLiteral("compareTargetButton"));
        QCOMPARE(buttons.size(), 2);
        for (QPushButton *button : buttons) {
            bool isNumber = false;
            button->text().toInt(&isNumber);
            QVERIFY(isNumber);

            const int titleCenterY = title->mapTo(cell, title->rect().center()).y();
            const int buttonCenterY = button->mapTo(cell, button->rect().center()).y();
            QVERIFY2(qAbs(titleCenterY - buttonCenterY) <= 4,
                     qPrintable(QString("titleCenterY=%1, buttonCenterY=%2")
                                    .arg(titleCenterY)
                                    .arg(buttonCenterY)));
        }
    }
}

void tst_ComparePanel::layout_shrinksFromSixToTwo_cellsExpand()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    CompareSession session;
    ImageLoader loader;
    ComparePanel panel(&session, nullptr, &loader);
    panel.resize(1400, 900);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    QList<QPair<QString, QString>> selected;
    for (int i = 0; i < 6; ++i) {
        const QString folder = root.filePath(QString("folder_%1").arg(i));
        QVERIFY(QDir().mkpath(folder));
        const QString imagePath = createImageInFolder(folder, "img.png", QColor::fromHsv((i * 35) % 360, 255, 255));
        QVERIFY(!imagePath.isEmpty());
        QVERIFY(session.addFolder(folder));
        selected.append({folder, imagePath});
    }
    panel.setSelectedImages(selected);
    QCoreApplication::processEvents();

    auto cells = findCells(panel);
    QCOMPARE(cells.size(), 6);
    int widthBefore = cells.first()->width();
    QVERIFY(widthBefore > 0);

    while (session.folderCount() > 2) {
        QVERIFY(session.removeFolderAt(session.folderCount() - 1));
    }
    QCoreApplication::processEvents();

    cells = findCells(panel);
    QCOMPARE(cells.size(), 2);

    int widthAfter = cells.first()->width();
    QVERIFY(widthAfter > widthBefore);
}

void tst_ComparePanel::resizeToFirstImage_toggleResizesOtherCells()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    CompareSession session;
    ImageLoader loader;
    ComparePanel panel(&session, nullptr, &loader);
    panel.resize(1200, 800);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    const QString folder0 = root.filePath("folder_0");
    const QString folder1 = root.filePath("folder_1");
    QVERIFY(QDir().mkpath(folder0));
    QVERIFY(QDir().mkpath(folder1));

    const QString image0Path = createImageInFolder(folder0, "img.png", Qt::red);
    QVERIFY(!image0Path.isEmpty());

    QImage image1(20, 10, QImage::Format_ARGB32);
    image1.fill(Qt::green);
    const QString image1Path = folder1 + "/img.png";
    QVERIFY(image1.save(image1Path));

    QVERIFY(session.addFolder(folder0));
    QVERIFY(session.addFolder(folder1));
    panel.setSelectedImages({{folder0, image0Path}, {folder1, image1Path}});
    QCoreApplication::processEvents();

    const auto widgets = panel.findChildren<ZoomableImageWidget *>();
    QCOMPARE(widgets.size(), 2);
    QTRY_COMPARE_WITH_TIMEOUT(widgets[0]->image().size(), QSize(32, 32), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(widgets[1]->image().size(), QSize(20, 10), 2000);

    auto *resizeCheckBox = panel.findChild<QCheckBox *>();
    QVERIFY(resizeCheckBox != nullptr);
    QCOMPARE(resizeCheckBox->isChecked(), false);

    resizeCheckBox->setChecked(true);
    QCoreApplication::processEvents();

    QTRY_COMPARE_WITH_TIMEOUT(widgets[0]->image().size(), QSize(32, 32), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(widgets[1]->image().size(), QSize(32, 32), 2000);
}

void tst_ComparePanel::markButton_clickPersistsSingleImage()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    CompareSession session;
    ImageLoader loader;
    ImageMarkManager marks;
    ComparePanel panel(&session, nullptr, &loader);
    panel.setImageMarkManager(&marks);
    panel.resize(1000, 700);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    const QString folder0 = root.filePath("folder_0");
    const QString folder1 = root.filePath("folder_1");
    QVERIFY(QDir().mkpath(folder0));
    QVERIFY(QDir().mkpath(folder1));
    const QString image0Path = createImageInFolder(folder0, "img.png", Qt::red);
    const QString image1Path = createImageInFolder(folder1, "img.png", Qt::green);
    QVERIFY(!image0Path.isEmpty());
    QVERIFY(!image1Path.isEmpty());

    QVERIFY(session.addFolder(folder0));
    QVERIFY(session.addFolder(folder1));
    panel.setSelectedImages({{folder0, image0Path}, {folder1, image1Path}});
    QCoreApplication::processEvents();

    const auto cells = sortedCells(panel);
    QCOMPARE(cells.size(), 2);
    auto *button = cells[0]->findChild<QPushButton *>(QStringLiteral("imageMarkButton_B"));
    QVERIFY(button != nullptr);

    QTest::mouseClick(button, Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(marks.markForImage(folder0, image0Path),
                              QStringLiteral("B"),
                              1000);
    QVERIFY(marks.markForImage(folder1, image1Path).isEmpty());
}

void tst_ComparePanel::markButton_ctrlClickMarksAllCurrentImages()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    CompareSession session;
    ImageLoader loader;
    ImageMarkManager marks;
    ComparePanel panel(&session, nullptr, &loader);
    panel.setImageMarkManager(&marks);
    panel.resize(1000, 700);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    QList<QPair<QString, QString>> selected;
    for (int i = 0; i < 3; ++i) {
        const QString folder = root.filePath(QString("folder_%1").arg(i));
        QVERIFY(QDir().mkpath(folder));
        const QString imagePath = createImageInFolder(
            folder,
            "img.png",
            QColor::fromHsv(i * 70, 255, 230));
        QVERIFY(!imagePath.isEmpty());
        QVERIFY(session.addFolder(folder));
        selected.append({folder, imagePath});
    }

    panel.setSelectedImages(selected);
    QCoreApplication::processEvents();

    const auto cells = sortedCells(panel);
    QCOMPARE(cells.size(), 3);
    auto *button = cells[1]->findChild<QPushButton *>(QStringLiteral("imageMarkButton_D"));
    QVERIFY(button != nullptr);

    QTest::mouseClick(button, Qt::LeftButton, Qt::ControlModifier);

    for (const auto &pair : selected) {
        QTRY_COMPARE_WITH_TIMEOUT(marks.markForImage(pair.first, pair.second),
                                  QStringLiteral("D"),
                                  1000);
    }
}

void tst_ComparePanel::markShortcut_marksAllCurrentImages()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    CompareSession session;
    ImageLoader loader;
    ImageMarkManager marks;
    ComparePanel panel(&session, nullptr, &loader);
    panel.setImageMarkManager(&marks);
    panel.resize(1000, 700);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    QList<QPair<QString, QString>> selected;
    for (int i = 0; i < 2; ++i) {
        const QString folder = root.filePath(QString("folder_%1").arg(i));
        QVERIFY(QDir().mkpath(folder));
        const QString imagePath = createImageInFolder(
            folder,
            "img.png",
            QColor::fromHsv(i * 80, 255, 230));
        QVERIFY(!imagePath.isEmpty());
        QVERIFY(session.addFolder(folder));
        selected.append({folder, imagePath});
    }

    panel.setSelectedImages(selected);
    panel.setFocus();
    QCoreApplication::processEvents();

    QTest::keyClick(&panel, Qt::Key_C);

    for (const auto &pair : selected) {
        QTRY_COMPARE_WITH_TIMEOUT(marks.markForImage(pair.first, pair.second),
                                  QStringLiteral("C"),
                                  1000);
    }
}

QTEST_MAIN(tst_ComparePanel)
#include "tst_ComparePanel.moc"
