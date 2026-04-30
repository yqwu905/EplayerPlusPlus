#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QImage>
#include <QFile>
#include <QCheckBox>
#include <QListView>
#include <QPushButton>
#include <QScrollBar>
#include <algorithm>

#include "models/CompareSession.h"
#include "models/ImageListModel.h"
#include "services/ImageLoader.h"
#include "services/ImageMarkManager.h"
#include "widgets/BrowsePanel.h"
#include "widgets/ThumbnailWidget.h"

using SelectedImages = QList<QPair<QString, QString>>;
Q_DECLARE_METATYPE(SelectedImages)

class tst_BrowsePanel : public QObject
{
    Q_OBJECT

private slots:
    void duplicateFolder_plainClickSelectionsAreIndependent();
    void ctrlClick_alignsSameIndexRowsAcrossColumns();
    void altClick_exactVsFuzzyFileNameMatch();
    void markButtons_clickPersistsAndCtrlClickMarksSameRow();
    void selection_preloadsPreviousAndNextThreeImages();
    void scrollableColumn_keepsHeaderControlsVisible();
    void virtualizedColumn_doesNotCreateThumbnailWidgetsForRows();

private:
    static QList<QListView *> sortedViews(BrowsePanel &panel);
    static void waitForRows(QListView *view, int rows);
    static void clickRow(QListView *view, int row, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    static void clickMarkButton(QListView *view,
                                int row,
                                int categoryIndex,
                                Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    static int rowByFileName(QListView *view, const QString &fileName);
    static bool isRowSelected(QListView *view, int row);
};

QList<QListView *> tst_BrowsePanel::sortedViews(BrowsePanel &panel)
{
    auto views = panel.findChildren<QListView *>(QStringLiteral("compareColumnListView"));
    std::sort(views.begin(), views.end(), [&panel](QListView *lhs, QListView *rhs) {
        return lhs->mapTo(&panel, QPoint(0, 0)).x() < rhs->mapTo(&panel, QPoint(0, 0)).x();
    });
    return views;
}

void tst_BrowsePanel::waitForRows(QListView *view, int rows)
{
    QVERIFY(view != nullptr);
    QTRY_COMPARE_WITH_TIMEOUT(view->model()->rowCount(), rows, 8000);
}

void tst_BrowsePanel::clickRow(QListView *view, int row, Qt::KeyboardModifiers modifiers)
{
    QVERIFY(view != nullptr);
    auto *model = view->model();
    QVERIFY(model != nullptr);
    QVERIFY(row >= 0 && row < model->rowCount());

    const QModelIndex index = model->index(row, 0);
    view->scrollTo(index, QAbstractItemView::PositionAtCenter);
    QTest::qWait(30);

    QRect rect = view->visualRect(index);
    QVERIFY2(rect.isValid(), qPrintable(QString("invalid visual rect for row %1").arg(row)));
    QTest::mouseClick(view->viewport(), Qt::LeftButton, modifiers, rect.center());
}

void tst_BrowsePanel::clickMarkButton(QListView *view,
                                      int row,
                                      int categoryIndex,
                                      Qt::KeyboardModifiers modifiers)
{
    QVERIFY(view != nullptr);
    QVERIFY(categoryIndex >= 0 && categoryIndex < 4);

    auto *model = view->model();
    QVERIFY(model != nullptr);
    QVERIFY(row >= 0 && row < model->rowCount());

    const QModelIndex index = model->index(row, 0);
    view->scrollTo(index, QAbstractItemView::PositionAtCenter);
    QTest::qWait(30);

    const QRect itemRect = view->visualRect(index);
    QVERIFY2(itemRect.isValid(), qPrintable(QString("invalid visual rect for row %1").arg(row)));

    constexpr int cardWidth = 194;
    constexpr int buttonSize = 18;
    constexpr int buttonGap = 3;
    constexpr int topMargin = 10;
    constexpr int rightMargin = 10;
    const int cardX = itemRect.x() + qMax(0, (itemRect.width() - cardWidth) / 2);
    const int cardY = itemRect.y() + 2;
    const int totalWidth = 4 * buttonSize + 3 * buttonGap;
    const int buttonX = cardX + cardWidth - rightMargin - totalWidth
                        + categoryIndex * (buttonSize + buttonGap);
    const QPoint point(buttonX + buttonSize / 2,
                       cardY + topMargin + buttonSize / 2);

    QTest::mouseClick(view->viewport(), Qt::LeftButton, modifiers, point);
}

int tst_BrowsePanel::rowByFileName(QListView *view, const QString &fileName)
{
    auto *model = qobject_cast<ImageListModel *>(view->model());
    if (!model) {
        return -1;
    }
    for (int i = 0; i < model->imageCount(); ++i) {
        if (model->fileNameAt(i) == fileName) {
            return i;
        }
    }
    return -1;
}

bool tst_BrowsePanel::isRowSelected(QListView *view, int row)
{
    auto *model = qobject_cast<ImageListModel *>(view->model());
    if (!model || row < 0 || row >= model->imageCount()) {
        return false;
    }
    return model->isSelected(row);
}

void tst_BrowsePanel::duplicateFolder_plainClickSelectionsAreIndependent()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString imageAPath = tempDir.filePath("a.png");
    const QString imageBPath = tempDir.filePath("b.png");

    QImage imageA(16, 16, QImage::Format_ARGB32);
    imageA.fill(Qt::red);
    QVERIFY(imageA.save(imageAPath));

    QImage imageB(16, 16, QImage::Format_ARGB32);
    imageB.fill(Qt::green);
    QVERIFY(imageB.save(imageBPath));

    CompareSession session;
    ImageLoader loader;
    BrowsePanel panel(&session, &loader);
    qRegisterMetaType<SelectedImages>("QList<QPair<QString,QString>>");
    panel.resize(900, 600);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    QSignalSpy selectionSpy(&panel, &BrowsePanel::selectionChanged);

    QVERIFY(session.addFolder(tempDir.path()));
    QVERIFY(session.addFolder(tempDir.path()));

    QList<QListView *> views;
    QTRY_VERIFY_WITH_TIMEOUT((views = sortedViews(panel), views.size() == 2), 5000);
    waitForRows(views[0], 2);
    waitForRows(views[1], 2);

    clickRow(views[0], 0);
    QTRY_VERIFY_WITH_TIMEOUT(selectionSpy.count() >= 1, 2000);

    clickRow(views[1], 1);
    QTRY_VERIFY_WITH_TIMEOUT(selectionSpy.count() >= 2, 2000);

    const auto latestSelection = qvariant_cast<SelectedImages>(selectionSpy.last().at(0));
    QCOMPARE(latestSelection.size(), 2);
    QVERIFY(latestSelection.contains(qMakePair(tempDir.path(), imageAPath)));
    QVERIFY(latestSelection.contains(qMakePair(tempDir.path(), imageBPath)));
}

void tst_BrowsePanel::ctrlClick_alignsSameIndexRowsAcrossColumns()
{
    QTemporaryDir dirA;
    QTemporaryDir dirB;
    QVERIFY(dirA.isValid());
    QVERIFY(dirB.isValid());

    for (int i = 0; i < 16; ++i) {
        const QString name = QString("img_%1.png").arg(i, 2, 10, QChar('0'));
        QImage image(16, 16, QImage::Format_ARGB32);
        image.fill(QColor::fromHsv((i * 23) % 360, 255, 200));
        QVERIFY(image.save(dirA.filePath(name)));
        QVERIFY(image.save(dirB.filePath(name)));
    }

    CompareSession session;
    ImageLoader loader;
    BrowsePanel panel(&session, &loader);
    panel.resize(900, 500);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    QVERIFY(session.addFolder(dirA.path()));
    QVERIFY(session.addFolder(dirB.path()));

    QList<QListView *> views;
    QTRY_VERIFY_WITH_TIMEOUT((views = sortedViews(panel), views.size() == 2), 5000);
    waitForRows(views[0], 16);
    waitForRows(views[1], 16);

    views[1]->verticalScrollBar()->setValue(views[1]->verticalScrollBar()->maximum());
    QVERIFY(views[1]->verticalScrollBar()->value() > 0);

    clickRow(views[0], 10, Qt::ControlModifier);

    const QModelIndex anchorIndex = views[0]->model()->index(10, 0);
    const QModelIndex matchedIndex = views[1]->model()->index(10, 0);
    const int anchorY = views[0]->visualRect(anchorIndex).top();
    const int matchedY = views[1]->visualRect(matchedIndex).top();
    QVERIFY2(qAbs(anchorY - matchedY) <= 4,
             qPrintable(QString("anchorY=%1, matchedY=%2").arg(anchorY).arg(matchedY)));
}

void tst_BrowsePanel::altClick_exactVsFuzzyFileNameMatch()
{
    QTemporaryDir dirA;
    QTemporaryDir dirB;
    QVERIFY(dirA.isValid());
    QVERIFY(dirB.isValid());

    QImage image(16, 16, QImage::Format_ARGB32);
    image.fill(Qt::yellow);

    const QString anchorName = "cat.png";
    const QString exactName = "cat.png";
    const QString fuzzyName = "cats.png";
    QVERIFY(image.save(dirA.filePath(anchorName)));
    QVERIFY(image.save(dirA.filePath("dog.png")));
    QVERIFY(image.save(dirB.filePath(exactName)));
    QVERIFY(image.save(dirB.filePath(fuzzyName)));

    CompareSession session;
    ImageLoader loader;
    BrowsePanel panel(&session, &loader);
    panel.resize(900, 500);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    QVERIFY(session.addFolder(dirA.path()));
    QVERIFY(session.addFolder(dirB.path()));

    QList<QListView *> views;
    QTRY_VERIFY_WITH_TIMEOUT((views = sortedViews(panel), views.size() == 2), 5000);
    waitForRows(views[0], 2);
    waitForRows(views[1], 2);

    const int anchorRow = rowByFileName(views[0], anchorName);
    const int exactRow = rowByFileName(views[1], exactName);
    const int fuzzyRow = rowByFileName(views[1], fuzzyName);
    QVERIFY(anchorRow >= 0);
    QVERIFY(exactRow >= 0);
    QVERIFY(fuzzyRow >= 0);

    clickRow(views[0], anchorRow, Qt::AltModifier);
    QTRY_VERIFY_WITH_TIMEOUT(isRowSelected(views[0], anchorRow), 1000);
    QVERIFY(isRowSelected(views[1], exactRow));
    QVERIFY(!isRowSelected(views[1], fuzzyRow));

    QVERIFY(QFile::remove(dirB.filePath(exactName)));
    QVERIFY(session.removeFolderAt(1));
    QVERIFY(session.addFolder(dirB.path()));

    QTRY_VERIFY_WITH_TIMEOUT((views = sortedViews(panel), views.size() == 2), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(
        ((views = sortedViews(panel)),
         views.size() == 2 &&
             ((views[0]->model()->rowCount() == 2 && views[1]->model()->rowCount() == 1) ||
              (views[0]->model()->rowCount() == 1 && views[1]->model()->rowCount() == 2))),
        8000);

    auto *fuzzyCheckBox = panel.findChild<QCheckBox *>("fuzzyFileNameCheckBox");
    QVERIFY(fuzzyCheckBox != nullptr);
    QVERIFY(!fuzzyCheckBox->isChecked());
    fuzzyCheckBox->setChecked(true);

    QListView *anchorView = nullptr;
    QListView *fuzzyView = nullptr;
    for (auto *view : views) {
        if (rowByFileName(view, anchorName) >= 0) {
            anchorView = view;
        }
        if (rowByFileName(view, fuzzyName) >= 0) {
            fuzzyView = view;
        }
    }
    QVERIFY(anchorView != nullptr);
    QVERIFY(fuzzyView != nullptr);

    const int newAnchorRow = rowByFileName(anchorView, anchorName);
    const int newFuzzyRow = rowByFileName(fuzzyView, fuzzyName);
    QVERIFY(newAnchorRow >= 0);
    QVERIFY(newFuzzyRow >= 0);

    clickRow(anchorView, newAnchorRow, Qt::AltModifier);
    QTRY_VERIFY_WITH_TIMEOUT(isRowSelected(fuzzyView, newFuzzyRow), 2000);
}

void tst_BrowsePanel::markButtons_clickPersistsAndCtrlClickMarksSameRow()
{
    QTemporaryDir dirA;
    QTemporaryDir dirB;
    QVERIFY(dirA.isValid());
    QVERIFY(dirB.isValid());

    QStringList pathsA;
    QStringList pathsB;
    for (int i = 0; i < 2; ++i) {
        const QString name = QString("img_%1.png").arg(i);
        QImage image(16, 16, QImage::Format_ARGB32);
        image.fill(QColor::fromHsv(i * 90, 255, 220));
        pathsA.append(dirA.filePath(name));
        pathsB.append(dirB.filePath(name));
        QVERIFY(image.save(pathsA.last()));
        QVERIFY(image.save(pathsB.last()));
    }

    CompareSession session;
    ImageLoader loader;
    ImageMarkManager marks;
    BrowsePanel panel(&session, &loader);
    panel.setImageMarkManager(&marks);
    panel.resize(900, 500);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    QVERIFY(session.addFolder(dirA.path()));
    QVERIFY(session.addFolder(dirB.path()));

    QList<QListView *> views;
    QTRY_VERIFY_WITH_TIMEOUT((views = sortedViews(panel), views.size() == 2), 5000);
    waitForRows(views[0], 2);
    waitForRows(views[1], 2);

    clickMarkButton(views[0], 0, 1); // B
    QTRY_COMPARE_WITH_TIMEOUT(marks.markForImage(dirA.path(), pathsA.at(0)),
                              QStringLiteral("B"),
                              1000);
    QVERIFY(marks.markForImage(dirB.path(), pathsB.at(0)).isEmpty());

    clickMarkButton(views[0], 1, 3, Qt::ControlModifier); // D
    QTRY_COMPARE_WITH_TIMEOUT(marks.markForImage(dirA.path(), pathsA.at(1)),
                              QStringLiteral("D"),
                              1000);
    QCOMPARE(marks.markForImage(dirB.path(), pathsB.at(1)), QStringLiteral("D"));

    clickRow(views[0], 0, Qt::ControlModifier);
    clickMarkButton(views[0], 1, 0, Qt::ControlModifier); // A, applies to current selection
    QTRY_COMPARE_WITH_TIMEOUT(marks.markForImage(dirA.path(), pathsA.at(0)),
                              QStringLiteral("A"),
                              1000);
    QCOMPARE(marks.markForImage(dirB.path(), pathsB.at(0)), QStringLiteral("A"));
    QCOMPARE(marks.markForImage(dirA.path(), pathsA.at(1)), QStringLiteral("D"));
    QCOMPARE(marks.markForImage(dirB.path(), pathsB.at(1)), QStringLiteral("D"));
}

void tst_BrowsePanel::selection_preloadsPreviousAndNextThreeImages()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QStringList imagePaths;
    for (int i = 0; i < 10; ++i) {
        const QString name = QString("img_%1.png").arg(i, 2, 10, QChar('0'));
        const QString path = dir.filePath(name);
        QImage image(24, 24, QImage::Format_ARGB32);
        image.fill(QColor::fromHsv((i * 36) % 360, 255, 210));
        QVERIFY(image.save(path));
        imagePaths.append(path);
    }

    CompareSession session;
    ImageLoader loader;
    BrowsePanel panel(&session, &loader);
    panel.resize(700, 600);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    QVERIFY(session.addFolder(dir.path()));

    QList<QListView *> views;
    QTRY_VERIFY_WITH_TIMEOUT((views = sortedViews(panel), views.size() == 1), 5000);
    waitForRows(views[0], 10);

    QSignalSpy imageSpy(&loader, &ImageLoader::imageReady);
    clickRow(views[0], 4);

    QTRY_VERIFY_WITH_TIMEOUT(imageSpy.count() >= 6, 5000);

    QSet<QString> loadedPaths;
    for (const auto &call : imageSpy) {
        loadedPaths.insert(call.at(0).toString());
    }

    QVERIFY(loadedPaths.contains(imagePaths.at(1)));
    QVERIFY(loadedPaths.contains(imagePaths.at(2)));
    QVERIFY(loadedPaths.contains(imagePaths.at(3)));
    QVERIFY(loadedPaths.contains(imagePaths.at(5)));
    QVERIFY(loadedPaths.contains(imagePaths.at(6)));
    QVERIFY(loadedPaths.contains(imagePaths.at(7)));
}

void tst_BrowsePanel::scrollableColumn_keepsHeaderControlsVisible()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    for (int i = 0; i < 20; ++i) {
        const QString name = QString("very_long_image_name_%1.png").arg(i, 2, 10, QChar('0'));
        QImage image(24, 24, QImage::Format_ARGB32);
        image.fill(QColor::fromHsv((i * 18) % 360, 255, 210));
        QVERIFY(image.save(dir.filePath(name)));
    }

    CompareSession session;
    ImageLoader loader;
    BrowsePanel panel(&session, &loader);
    panel.resize(260, 420);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    QVERIFY(session.addFolder(dir.path()));

    QList<QListView *> views;
    QTRY_VERIFY_WITH_TIMEOUT((views = sortedViews(panel), views.size() == 1), 5000);
    waitForRows(views[0], 20);
    QVERIFY(views[0]->verticalScrollBar()->maximum() > 0);

    auto *closeButton = panel.findChild<QPushButton *>("compareColumnCloseButton");
    QVERIFY(closeButton != nullptr);

    views[0]->verticalScrollBar()->setValue(views[0]->verticalScrollBar()->maximum());
    QTest::qWait(30);

    const QRect closeRect(closeButton->mapTo(&panel, QPoint(0, 0)), closeButton->size());
    QVERIFY2(panel.rect().contains(closeRect),
             qPrintable(QString("closeRect=%1,%2 %3x%4 panel=%5,%6 %7x%8")
                            .arg(closeRect.x())
                            .arg(closeRect.y())
                            .arg(closeRect.width())
                            .arg(closeRect.height())
                            .arg(panel.rect().x())
                            .arg(panel.rect().y())
                            .arg(panel.rect().width())
                            .arg(panel.rect().height())));
}

void tst_BrowsePanel::virtualizedColumn_doesNotCreateThumbnailWidgetsForRows()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    for (int i = 0; i < 200; ++i) {
        const QString name = QString("img_%1.png").arg(i, 3, 10, QChar('0'));
        QImage image(12, 12, QImage::Format_ARGB32);
        image.fill(QColor::fromHsv((i * 11) % 360, 255, 210));
        QVERIFY(image.save(dir.filePath(name)));
    }

    CompareSession session;
    ImageLoader loader;
    BrowsePanel panel(&session, &loader);
    panel.resize(320, 500);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    QVERIFY(session.addFolder(dir.path()));

    QList<QListView *> views;
    QTRY_VERIFY_WITH_TIMEOUT((views = sortedViews(panel), views.size() == 1), 5000);
    waitForRows(views[0], 200);
    QCOMPARE(panel.findChildren<ThumbnailWidget *>().size(), 0);
}

QTEST_MAIN(tst_BrowsePanel)
#include "tst_BrowsePanel.moc"
