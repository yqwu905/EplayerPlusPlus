#include <QTest>
#include <QTemporaryDir>
#include <QImage>
#include <QSet>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QDir>
#include <QCoreApplication>
#include <QApplication>
#include <QInputDialog>
#include <QLineEdit>
#include <QTimer>
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
    void compareHeader_placesTitleAndButtonsInHeaderBlock();
    void layout_imageAreaExpandsToFillAvailableGridHeight();
    void customGridName_updatesOnlyThatCellAndCompareTooltips();
    void folderSwap_reordersGridCellsAndImages();
    void layout_shrinksFromSixToTwo_cellsExpand();
    void toleranceMode_compareButtonTogglesTargetImage();
    void toleranceMode_usesPreviewWhenFullImageIsNotLoaded();
    void resizeToFirstImage_toggleResizesOtherCells();
    void resizeToFirstImage_refreshesLoadedCellsWhenFirstFullImageArrives();
    void markButton_clickPersistsSingleImage();
    void markButton_ctrlClickMarksAllCurrentImages();
    void markShortcut_marksAllCurrentImages();

private:
    static QString createImageInFolder(const QString &folderPath, const QString &name, const QColor &color);
    static QList<QWidget *> findCells(ComparePanel &panel);
    static QList<QWidget *> sortedCells(ComparePanel &panel);
    static void acceptNextRenameDialog(const QString &name);
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

void tst_ComparePanel::acceptNextRenameDialog(const QString &name)
{
    QTimer::singleShot(0, [name]() {
        QInputDialog *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
        if (!dialog) {
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                dialog = qobject_cast<QInputDialog *>(widget);
                if (dialog) {
                    break;
                }
            }
        }

        if (!dialog) {
            return;
        }

        if (auto *lineEdit = dialog->findChild<QLineEdit *>()) {
            lineEdit->setText(name);
        }
        dialog->accept();
    });
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

void tst_ComparePanel::compareHeader_placesTitleAndButtonsInHeaderBlock()
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

            const int titleTop = title->mapTo(cell, title->rect().topLeft()).y();
            const int titleBottom = title->mapTo(cell, title->rect().bottomLeft()).y();
            const int buttonTop = button->mapTo(cell, button->rect().topLeft()).y();
            QVERIFY2(buttonTop >= titleTop,
                     qPrintable(QString("titleTop=%1, buttonTop=%2")
                                    .arg(titleTop)
                                    .arg(buttonTop)));
            QVERIFY2(buttonTop - titleBottom <= 28,
                     qPrintable(QString("titleBottom=%1, buttonTop=%2")
                                    .arg(titleBottom)
                                    .arg(buttonTop)));
        }
    }
}

void tst_ComparePanel::layout_imageAreaExpandsToFillAvailableGridHeight()
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
    for (int i = 0; i < 2; ++i) {
        const QString folder = root.filePath(QString("folder_%1").arg(i));
        QVERIFY(QDir().mkpath(folder));
        const QString imagePath = createImageInFolder(
            folder,
            QString("image_%1.png").arg(i),
            QColor::fromHsv(i * 80, 255, 230));
        QVERIFY(!imagePath.isEmpty());
        QVERIFY(session.addFolder(folder));
        selected.append({folder, imagePath});
    }
    panel.setSelectedImages(selected);
    QCoreApplication::processEvents();

    const auto cells = findCells(panel);
    QCOMPARE(cells.size(), 2);

    for (QWidget *cell : cells) {
        auto *imageWidget = cell->findChild<ZoomableImageWidget *>();
        QVERIFY(imageWidget != nullptr);
        QVERIFY2(imageWidget->height() > 300,
                 qPrintable(QString("image height should exceed the old cap, got %1")
                                .arg(imageWidget->height())));
    }
}

void tst_ComparePanel::customGridName_updatesOnlyThatCellAndCompareTooltips()
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
    for (int i = 0; i < 2; ++i) {
        const QString folder = root.filePath(QString("folder_%1").arg(i));
        QVERIFY(QDir().mkpath(folder));
        const QString imagePath = createImageInFolder(
            folder,
            QString("image_%1.png").arg(i),
            QColor::fromHsv(i * 80, 255, 230));
        QVERIFY(!imagePath.isEmpty());
        QVERIFY(session.addFolder(folder));
        selected.append({folder, imagePath});
    }
    panel.setSelectedImages(selected);
    QCoreApplication::processEvents();

    const auto cells = sortedCells(panel);
    QCOMPARE(cells.size(), 2);

    auto *firstRename = cells[0]->findChild<QPushButton *>(
        QStringLiteral("compareCellRenameButton"));
    auto *secondRename = cells[1]->findChild<QPushButton *>(
        QStringLiteral("compareCellRenameButton"));
    QVERIFY(firstRename != nullptr);
    QVERIFY(secondRename != nullptr);

    acceptNextRenameDialog(QStringLiteral("Baseline"));
    QTest::mouseClick(firstRename, Qt::LeftButton);
    acceptNextRenameDialog(QStringLiteral("Candidate"));
    QTest::mouseClick(secondRename, Qt::LeftButton);
    QCoreApplication::processEvents();

    auto *firstTitle = cells[0]->findChild<QLabel *>(QStringLiteral("compareCellHeaderLabel"));
    auto *secondTitle = cells[1]->findChild<QLabel *>(QStringLiteral("compareCellHeaderLabel"));
    QVERIFY(firstTitle != nullptr);
    QVERIFY(secondTitle != nullptr);
    QVERIFY(firstTitle->text().contains(QStringLiteral("Baseline")));
    QVERIFY(!firstTitle->text().contains(QStringLiteral("Candidate")));
    QVERIFY(secondTitle->text().contains(QStringLiteral("Candidate")));
    QVERIFY(!secondTitle->text().contains(QStringLiteral("Baseline")));

    const auto firstButtons = cells[0]->findChildren<QPushButton *>(
        QStringLiteral("compareTargetButton"));
    const auto secondButtons = cells[1]->findChildren<QPushButton *>(
        QStringLiteral("compareTargetButton"));
    QCOMPARE(firstButtons.size(), 1);
    QCOMPARE(secondButtons.size(), 1);
    QVERIFY(firstButtons.first()->toolTip().contains(QStringLiteral("Candidate")));
    QVERIFY(secondButtons.first()->toolTip().contains(QStringLiteral("Baseline")));
}

void tst_ComparePanel::folderSwap_reordersGridCellsAndImages()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    CompareSession session;
    ComparePanel panel(&session, nullptr, nullptr);
    panel.resize(1100, 700);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    QList<QPair<QString, QString>> selected;
    const QList<QColor> colors = {Qt::red, Qt::green, Qt::blue};
    for (int i = 0; i < colors.size(); ++i) {
        const QString folder = root.filePath(QString("folder_%1").arg(i));
        QVERIFY(QDir().mkpath(folder));
        const QString imagePath = createImageInFolder(
            folder,
            QString("image_%1.png").arg(i),
            colors.at(i));
        QVERIFY(!imagePath.isEmpty());
        QVERIFY(session.addFolder(folder));
        selected.append({folder, imagePath});
    }

    panel.setSelectedImages(selected);
    QCoreApplication::processEvents();

    auto cells = sortedCells(panel);
    QCOMPARE(cells.size(), 3);

    auto *firstTitle = cells[0]->findChild<QLabel *>(QStringLiteral("compareCellHeaderLabel"));
    auto *thirdTitle = cells[2]->findChild<QLabel *>(QStringLiteral("compareCellHeaderLabel"));
    QVERIFY(firstTitle != nullptr);
    QVERIFY(thirdTitle != nullptr);
    QVERIFY(firstTitle->text().contains(QStringLiteral("image_0.png")));
    QVERIFY(thirdTitle->text().contains(QStringLiteral("image_2.png")));

    QVERIFY(session.swapFolders(0, 2));
    QCoreApplication::processEvents();

    cells = sortedCells(panel);
    QCOMPARE(cells.size(), 3);

    firstTitle = cells[0]->findChild<QLabel *>(QStringLiteral("compareCellHeaderLabel"));
    thirdTitle = cells[2]->findChild<QLabel *>(QStringLiteral("compareCellHeaderLabel"));
    QVERIFY(firstTitle != nullptr);
    QVERIFY(thirdTitle != nullptr);
    QVERIFY(firstTitle->text().contains(QStringLiteral("image_2.png")));
    QVERIFY(thirdTitle->text().contains(QStringLiteral("image_0.png")));

    auto *firstImage = cells[0]->findChild<ZoomableImageWidget *>();
    auto *thirdImage = cells[2]->findChild<ZoomableImageWidget *>();
    QVERIFY(firstImage != nullptr);
    QVERIFY(thirdImage != nullptr);
    QCOMPARE(firstImage->image().pixelColor(0, 0), QColor(Qt::blue));
    QCOMPARE(thirdImage->image().pixelColor(0, 0), QColor(Qt::red));

    auto *firstBadge = cells[0]->findChild<QLabel *>(QStringLiteral("compareCellIndexBadge"));
    auto *thirdBadge = cells[2]->findChild<QLabel *>(QStringLiteral("compareCellIndexBadge"));
    QVERIFY(firstBadge != nullptr);
    QVERIFY(thirdBadge != nullptr);
    QCOMPARE(firstBadge->text(), QStringLiteral("1"));
    QCOMPARE(thirdBadge->text(), QStringLiteral("3"));
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

void tst_ComparePanel::toleranceMode_compareButtonTogglesTargetImage()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    CompareSession session;
    ComparePanel panel(&session, nullptr, nullptr);
    panel.resize(900, 600);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    QList<QPair<QString, QString>> selected;
    const QString folderA = root.filePath(QStringLiteral("folder_a"));
    const QString folderB = root.filePath(QStringLiteral("folder_b"));
    QVERIFY(QDir().mkpath(folderA));
    QVERIFY(QDir().mkpath(folderB));
    const QString imageA = createImageInFolder(folderA, QStringLiteral("img.png"), Qt::red);
    const QString imageB = createImageInFolder(folderB, QStringLiteral("img.png"), Qt::blue);
    QVERIFY(!imageA.isEmpty());
    QVERIFY(!imageB.isEmpty());
    QVERIFY(session.addFolder(folderA));
    QVERIFY(session.addFolder(folderB));
    selected.append({folderA, imageA});
    selected.append({folderB, imageB});
    panel.setSelectedImages(selected);
    QCoreApplication::processEvents();

    const auto cells = sortedCells(panel);
    QCOMPARE(cells.size(), 2);

    auto *targetImageWidget = cells.at(1)->findChild<ZoomableImageWidget *>();
    QVERIFY(targetImageWidget != nullptr);
    QCOMPARE(targetImageWidget->image().pixelColor(0, 0), QColor(Qt::blue));

    panel.setCompareMode(ComparePanel::ToleranceMode);
    auto *compareToSecondButton = cells.at(0)->findChild<QPushButton *>(
        QStringLiteral("compareTargetButton"));
    QVERIFY(compareToSecondButton != nullptr);
    QCOMPARE(compareToSecondButton->text(), QStringLiteral("2"));

    QTest::mouseClick(compareToSecondButton, Qt::LeftButton);
    QCoreApplication::processEvents();
    QVERIFY(targetImageWidget->image().pixelColor(0, 0) != QColor(Qt::blue));

    QTest::mouseClick(compareToSecondButton, Qt::LeftButton);
    QCoreApplication::processEvents();
    QCOMPARE(targetImageWidget->image().pixelColor(0, 0), QColor(Qt::blue));
}

void tst_ComparePanel::toleranceMode_usesPreviewWhenFullImageIsNotLoaded()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    CompareSession session;
    ImageLoader loader;
    ComparePanel panel(&session, nullptr, &loader);
    panel.resize(900, 600);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    const QString folderA = root.filePath(QStringLiteral("folder_a"));
    const QString folderB = root.filePath(QStringLiteral("folder_b"));
    QVERIFY(QDir().mkpath(folderA));
    QVERIFY(QDir().mkpath(folderB));
    QVERIFY(session.addFolder(folderA));
    QVERIFY(session.addFolder(folderB));

    const QString imageA = folderA + QStringLiteral("/not_loaded_a.png");
    const QString imageB = folderB + QStringLiteral("/not_loaded_b.png");
    panel.setSelectedImages({{folderA, imageA}, {folderB, imageB}});

    QImage previewA(32, 32, QImage::Format_ARGB32);
    previewA.fill(Qt::red);
    QImage previewB(32, 32, QImage::Format_ARGB32);
    previewB.fill(Qt::blue);
    QVERIFY(QMetaObject::invokeMethod(&panel, "onThumbnailReady",
                                      Qt::DirectConnection,
                                      Q_ARG(QString, imageA),
                                      Q_ARG(QImage, previewA)));
    QVERIFY(QMetaObject::invokeMethod(&panel, "onThumbnailReady",
                                      Qt::DirectConnection,
                                      Q_ARG(QString, imageB),
                                      Q_ARG(QImage, previewB)));
    QCoreApplication::processEvents();

    const auto cells = sortedCells(panel);
    QCOMPARE(cells.size(), 2);

    auto *targetImageWidget = cells.at(1)->findChild<ZoomableImageWidget *>();
    QVERIFY(targetImageWidget != nullptr);
    QCOMPARE(targetImageWidget->image().pixelColor(0, 0), QColor(Qt::blue));

    panel.setCompareMode(ComparePanel::ToleranceMode);
    auto *compareToSecondButton = cells.at(0)->findChild<QPushButton *>(
        QStringLiteral("compareTargetButton"));
    QVERIFY(compareToSecondButton != nullptr);
    QCOMPARE(compareToSecondButton->text(), QStringLiteral("2"));

    QTest::mouseClick(compareToSecondButton, Qt::LeftButton);
    QCoreApplication::processEvents();
    QVERIFY(targetImageWidget->image().pixelColor(0, 0) != QColor(Qt::blue));
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

void tst_ComparePanel::resizeToFirstImage_refreshesLoadedCellsWhenFirstFullImageArrives()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    CompareSession session;
    ImageLoader loader;
    ComparePanel panel(&session, nullptr, &loader);
    panel.setResizeToFirstImageEnabled(true);
    panel.resize(1200, 800);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    const QString folder0 = root.filePath("folder_0");
    const QString folder1 = root.filePath("folder_1");
    QVERIFY(QDir().mkpath(folder0));
    QVERIFY(QDir().mkpath(folder1));
    QVERIFY(session.addFolder(folder0));
    QVERIFY(session.addFolder(folder1));

    const QString firstPath = folder0 + "/pending_first.png";
    const QString secondPath = folder1 + "/pending_second.png";
    panel.setSelectedImages({{folder0, firstPath}, {folder1, secondPath}});
    QCoreApplication::processEvents();

    QImage firstPreview(20, 20, QImage::Format_ARGB32);
    firstPreview.fill(Qt::red);
    QImage secondFullImage(80, 60, QImage::Format_ARGB32);
    secondFullImage.fill(Qt::green);
    QImage firstFullImage(100, 100, QImage::Format_ARGB32);
    firstFullImage.fill(Qt::blue);

    QVERIFY(QMetaObject::invokeMethod(&panel, "onThumbnailReady",
                                      Qt::DirectConnection,
                                      Q_ARG(QString, firstPath),
                                      Q_ARG(QImage, firstPreview)));
    QVERIFY(QMetaObject::invokeMethod(&panel, "onImageReady",
                                      Qt::DirectConnection,
                                      Q_ARG(QString, secondPath),
                                      Q_ARG(QImage, secondFullImage)));
    QCoreApplication::processEvents();

    const auto cells = sortedCells(panel);
    QCOMPARE(cells.size(), 2);
    auto *firstWidget = cells.at(0)->findChild<ZoomableImageWidget *>();
    auto *secondWidget = cells.at(1)->findChild<ZoomableImageWidget *>();
    QVERIFY(firstWidget != nullptr);
    QVERIFY(secondWidget != nullptr);
    QCOMPARE(firstWidget->image().size(), QSize(20, 20));
    QCOMPARE(secondWidget->image().size(), QSize(80, 60));

    QVERIFY(QMetaObject::invokeMethod(&panel, "onImageReady",
                                      Qt::DirectConnection,
                                      Q_ARG(QString, firstPath),
                                      Q_ARG(QImage, firstFullImage)));
    QCoreApplication::processEvents();

    QCOMPARE(firstWidget->image().size(), QSize(100, 100));
    QCOMPARE(secondWidget->image().size(), QSize(100, 100));
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
