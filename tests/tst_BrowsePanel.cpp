#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QImage>
#include <QFile>
#include <QAbstractItemDelegate>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QScrollBar>
#include <QScrollArea>
#include <QSet>
#include <QStyleOption>
#include <algorithm>
#include <utility>

#include "models/CompareSession.h"
#include "models/ImageListModel.h"
#include "services/ImageLoader.h"
#include "services/ImageMarkManager.h"
#include "styles/FluentStyle.h"
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
    void narrowColumn_marksStayInsideCardAndCanBeClicked();
    void filters_fileNameAndCategoryLimitVisibleRows();
    void filters_textFitsStyledControls_data();
    void filters_textFitsStyledControls();
    void fileNameFilter_debouncesRapidTyping();
    void categoryFilter_markingSelectedImageSelectsNextVisibleRow();
    void categoryFilter_ctrlClickAnchorsSameIndexRowsAcrossColumns();
    void categoryFilter_altClickAnchorsFuzzyFileNameMatchesAcrossColumns();
    void categoryFilter_fuzzyLargeListUsesBoundedIndexAndPreservesTies();
    void selection_preloadsPreviousAndNextThreeImages();
    void altMatchedNavigation_advancesAnchorAndRematchesOtherFolders();
    void independentNavigation_movesEachSelectedFolderSeparately();
    void folderSwap_reordersColumnsAndKeepsSelections();
    void scrollableColumn_keepsHeaderControlsVisible();
    void virtualizedColumn_doesNotCreateThumbnailWidgetsForRows();
    void thumbnailSize_scalesWithPanelWidthAndPushesDecodeBucket();
    void multipleFolders_panelMinimumStaysShrinkable();
    void multipleFolders_thumbnailsPaintWithoutResizing_data();
    void multipleFolders_thumbnailsPaintWithoutResizing();
    void moreThanSixFolders_addsAllColumnsWithHorizontalScroll();

private:
    static QList<QListView *> sortedViews(BrowsePanel &panel);
    static void waitForRows(QListView *view, int rows);
    static void clickRow(QListView *view, int row, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    static void clickMarkButton(QListView *view,
                                int row,
                                int categoryIndex,
                                Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    static QString folderPathForView(QListView *view);
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
    auto *panel = qobject_cast<BrowsePanel *>(view->window());
    QVERIFY(panel != nullptr);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, modifiers,
                      panel->thumbnailMetrics().thumbnailImageRect(rect).center());
}

void tst_BrowsePanel::clickMarkButton(QListView *view,
                                      int row,
                                      int categoryIndex,
                                      Qt::KeyboardModifiers modifiers)
{
    QVERIFY(view != nullptr);
    const int categoryCount = ImageMarkManager::categories().size();
    QVERIFY(categoryIndex >= 0 && categoryIndex < categoryCount);

    auto *model = view->model();
    QVERIFY(model != nullptr);
    QVERIFY(row >= 0 && row < model->rowCount());

    const QModelIndex index = model->index(row, 0);
    view->scrollTo(index, QAbstractItemView::PositionAtCenter);
    QTest::qWait(30);

    const QRect itemRect = view->visualRect(index);
    QVERIFY2(itemRect.isValid(), qPrintable(QString("invalid visual rect for row %1").arg(row)));

    auto *panel = qobject_cast<BrowsePanel *>(view->window());
    QVERIFY(panel != nullptr);
    const QRect button = panel->thumbnailMetrics().markButtonRect(itemRect, categoryIndex);
    QVERIFY(view->viewport()->rect().contains(button));
    QTest::mouseClick(view->viewport(), Qt::LeftButton, modifiers, button.center());
}

QString tst_BrowsePanel::folderPathForView(QListView *view)
{
    auto *model = qobject_cast<ImageListModel *>(view ? view->model() : nullptr);
    return model ? model->folderPath() : QString();
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

    // Fuzzy matching is now driven from the command bar via this setter rather
    // than an in-panel checkbox.
    panel.setFuzzyFileNameMatchEnabled(true);

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

    clickMarkButton(views[0], 0, 1); // B again clears the active category
    QTRY_VERIFY_WITH_TIMEOUT(marks.markForImage(dirA.path(), pathsA.at(0)).isEmpty(),
                             1000);

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

void tst_BrowsePanel::narrowColumn_marksStayInsideCardAndCanBeClicked()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString imagePath = dir.filePath(QStringLiteral("image.png"));
    QImage image(64, 40, QImage::Format_RGB32);
    image.fill(QColor(QStringLiteral("#e32191")));
    QVERIFY(image.save(imagePath));

    CompareSession session;
    ImageLoader loader;
    ImageMarkManager marks;
    BrowsePanel panel(&session, &loader);
    panel.setImageMarkManager(&marks);
    panel.resize(320, 500);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    const QStringList categories = ImageMarkManager::categories();
    QVERIFY(marks.setMarkForImage(dir.path(), imagePath, categories.last()));
    for (int i = 0; i < 5; ++i) {
        QVERIFY(session.addFolder(dir.path()));
    }
    const auto views = sortedViews(panel);
    QCOMPARE(views.size(), 5);
    auto *view = views.first();
    waitForRows(view, 1);
    const QModelIndex index = view->model()->index(0, 0);
    QTRY_VERIFY(!index.data(ImageListModel::ThumbnailRole).value<QImage>().isNull());
    clickRow(view, 0);
    QVERIFY(isRowSelected(view, 0));

    const QRect itemRect = view->visualRect(index);
    const auto metrics = panel.thumbnailMetrics();
    const QRect card = metrics.thumbnailCardRect(itemRect);
    const QRect picture = metrics.thumbnailImageRect(itemRect);
    QVERIFY(card.width() < 6 * 18 + 5 * 3);
    QVERIFY(metrics.markButtonRect(itemRect, 0).top()
            < metrics.markButtonRect(itemRect, categories.size() - 1).top());

    QList<QRect> buttons;
    for (int i = 0; i < categories.size(); ++i) {
        const QRect button = metrics.markButtonRect(itemRect, i);
        QVERIFY(card.contains(button));
        QVERIFY(view->viewport()->rect().contains(button));
        QVERIFY(!button.intersects(picture));
        for (const QRect &previousButton : std::as_const(buttons)) {
            QVERIFY(!button.intersects(previousButton));
        }
        buttons.append(button);

        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, button.center());
        QTRY_COMPARE(marks.markForImage(dir.path(), imagePath), categories.at(i));
        QVERIFY(isRowSelected(view, 0));

        // Sample the active button's fill away from its letter and rounded corners.
        const QPixmap capture = view->viewport()->grab();
        const QPoint sample(button.left() + 2, button.center().y());
        const QPoint pixel = sample * capture.devicePixelRatio();
        QCOMPARE(capture.toImage().pixelColor(pixel), QColor(QStringLiteral("#0078d4")));
    }
}

void tst_BrowsePanel::filters_fileNameAndCategoryLimitVisibleRows()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QImage image(16, 16, QImage::Format_ARGB32);
    image.fill(Qt::darkYellow);
    const QString alphaCatPath = dir.filePath("alpha_cat.png");
    const QString betaCatPath = dir.filePath("beta_cat.png");
    const QString alphaDogPath = dir.filePath("alpha_dog.png");
    const QString plainPath = dir.filePath("plain.png");
    QVERIFY(image.save(alphaCatPath));
    QVERIFY(image.save(betaCatPath));
    QVERIFY(image.save(alphaDogPath));
    QVERIFY(image.save(plainPath));

    ImageMarkManager marks;
    QVERIFY(marks.setMarkForImage(dir.path(), alphaCatPath, "A"));
    QVERIFY(marks.setMarkForImage(dir.path(), alphaDogPath, "A"));
    QVERIFY(marks.setMarkForImage(dir.path(), betaCatPath, "B"));

    CompareSession session;
    ImageLoader loader;
    BrowsePanel panel(&session, &loader);
    panel.setImageMarkManager(&marks);
    panel.resize(700, 500);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    QVERIFY(session.addFolder(dir.path()));

    QList<QListView *> views;
    QTRY_VERIFY_WITH_TIMEOUT((views = sortedViews(panel), views.size() == 1), 5000);
    waitForRows(views[0], 4);

    auto *fileFilter = panel.findChild<QLineEdit *>("fileNameFilterEdit");
    auto *categoryFilter = panel.findChild<QComboBox *>("categoryFilterComboBox");
    QVERIFY(fileFilter != nullptr);
    QVERIFY(categoryFilter != nullptr);

    fileFilter->setText(QStringLiteral("cat"));
    QTRY_COMPARE_WITH_TIMEOUT(views[0]->model()->rowCount(), 2, 1000);
    QVERIFY(rowByFileName(views[0], QStringLiteral("alpha_cat.png")) >= 0);
    QVERIFY(rowByFileName(views[0], QStringLiteral("beta_cat.png")) >= 0);
    QCOMPARE(rowByFileName(views[0], QStringLiteral("alpha_dog.png")), -1);

    const int categoryAIndex = categoryFilter->findData(QStringLiteral("A"));
    QVERIFY(categoryAIndex >= 0);
    categoryFilter->setCurrentIndex(categoryAIndex);
    QTRY_COMPARE_WITH_TIMEOUT(views[0]->model()->rowCount(), 1, 1000);
    QCOMPARE(rowByFileName(views[0], QStringLiteral("alpha_cat.png")), 0);

    fileFilter->clear();
    QTRY_COMPARE_WITH_TIMEOUT(views[0]->model()->rowCount(), 2, 1000);
    QVERIFY(rowByFileName(views[0], QStringLiteral("alpha_cat.png")) >= 0);
    QVERIFY(rowByFileName(views[0], QStringLiteral("alpha_dog.png")) >= 0);
}

void tst_BrowsePanel::filters_textFitsStyledControls_data()
{
    QTest::addColumn<int>("pointSize");
    QTest::newRow("platform-default") << 0;
    QTest::newRow("application-font-size") << FluentStyle::fontSizeBody();
    QTest::newRow("larger-font") << 18;
}

void tst_BrowsePanel::filters_textFitsStyledControls()
{
    QFETCH(int, pointSize);
    CompareSession session;
    ImageLoader loader;
    BrowsePanel panel(&session, &loader);
    if (pointSize > 0) {
        QFont font = panel.font();
        font.setPointSize(pointSize);
        panel.setFont(font);
    }
    panel.resize(430, 500);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    auto *combo = panel.findChild<QComboBox *>(QStringLiteral("categoryFilterComboBox"));
    auto *edit = panel.findChild<QLineEdit *>(QStringLiteral("fileNameFilterEdit"));
    QVERIFY(combo != nullptr);
    QVERIFY(edit != nullptr);

    // Check the real styled text rectangles both on the default-width row
    // and when the same controls have to wrap at the layout's minimum width.
    for (int panelWidth : {430, panel.minimumSizeHint().width()}) {
        panel.resize(panelWidth, 500);
        QCoreApplication::processEvents();
        QVERIFY(panel.rect().contains(combo->geometry()));
        QVERIFY(panel.rect().contains(edit->geometry()));
        QVERIFY(!combo->geometry().intersects(edit->geometry()));
        QCOMPARE(combo->y() == edit->y(), panelWidth == 430);

        for (int i = 0; i < combo->count(); ++i) {
            combo->setCurrentIndex(i);
            QStyleOptionComboBox option;
            option.initFrom(combo);
            option.currentText = combo->currentText();
            const QRect textRect = combo->style()->subControlRect(
                QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, combo);
            const QRect arrowRect = combo->style()->subControlRect(
                QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxArrow, combo);
            const int textWidth = combo->fontMetrics().horizontalAdvance(combo->currentText());
            QVERIFY2(textRect.width() >= textWidth + 4,
                     qPrintable(QStringLiteral("'%1': text %2px, field %3px")
                                    .arg(combo->currentText()).arg(textWidth).arg(textRect.width())));
            QVERIFY(!textRect.intersects(arrowRect));
        }

        QStyleOptionFrame option;
        option.initFrom(edit);
        const QRect textRect = edit->style()->subElementRect(QStyle::SE_LineEditContents,
                                                            &option, edit);
        const int placeholderWidth = edit->fontMetrics().horizontalAdvance(edit->placeholderText());
        QVERIFY(textRect.width() >= placeholderWidth + 4);
    }
}

void tst_BrowsePanel::fileNameFilter_debouncesRapidTyping()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QImage image(12, 12, QImage::Format_ARGB32);
    image.fill(Qt::green);
    QVERIFY(image.save(dir.filePath(QStringLiteral("alpha.png"))));
    QVERIFY(image.save(dir.filePath(QStringLiteral("beta.png"))));

    CompareSession session;
    ImageLoader loader;
    BrowsePanel panel(&session, &loader);
    panel.resize(600, 400);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    QVERIFY(session.addFolder(dir.path()));

    QList<QListView *> views;
    QTRY_VERIFY_WITH_TIMEOUT((views = sortedViews(panel), views.size() == 1), 5000);
    waitForRows(views[0], 2);
    auto *filter = panel.findChild<QLineEdit *>(QStringLiteral("fileNameFilterEdit"));
    QVERIFY(filter != nullptr);
    QSignalSpy resetSpy(views[0]->model(), &QAbstractItemModel::modelReset);

    filter->setText(QStringLiteral("a"));
    filter->setText(QStringLiteral("al"));
    filter->setText(QStringLiteral("alp"));
    QTRY_COMPARE_WITH_TIMEOUT(views[0]->model()->rowCount(), 1, 1000);
    QTest::qWait(160);
    QCOMPARE(resetSpy.count(), 1);
}

void tst_BrowsePanel::categoryFilter_markingSelectedImageSelectsNextVisibleRow()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QImage image(16, 16, QImage::Format_ARGB32);
    image.fill(Qt::darkBlue);
    QStringList paths;
    for (int i = 0; i < 3; ++i) {
        const QString path = dir.filePath(QString("img_%1.png").arg(i, 2, 10, QChar('0')));
        QVERIFY(image.save(path));
        paths.append(path);
    }

    ImageMarkManager marks;
    for (const QString &path : paths) {
        QVERIFY(marks.setMarkForImage(dir.path(), path, "A"));
    }

    CompareSession session;
    ImageLoader loader;
    BrowsePanel panel(&session, &loader);
    panel.setImageMarkManager(&marks);
    panel.resize(700, 500);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    QVERIFY(session.addFolder(dir.path()));

    QList<QListView *> views;
    QTRY_VERIFY_WITH_TIMEOUT((views = sortedViews(panel), views.size() == 1), 5000);
    waitForRows(views[0], 3);

    auto *categoryFilter = panel.findChild<QComboBox *>("categoryFilterComboBox");
    QVERIFY(categoryFilter != nullptr);
    const int categoryAIndex = categoryFilter->findData(QStringLiteral("A"));
    QVERIFY(categoryAIndex >= 0);
    categoryFilter->setCurrentIndex(categoryAIndex);
    QTRY_COMPARE_WITH_TIMEOUT(views[0]->model()->rowCount(), 3, 1000);

    clickRow(views[0], 0);
    QTRY_VERIFY_WITH_TIMEOUT(isRowSelected(views[0], 0), 1000);

    clickMarkButton(views[0], 0, 1); // B
    QTRY_COMPARE_WITH_TIMEOUT(views[0]->model()->rowCount(), 2, 1000);
    QCOMPARE(rowByFileName(views[0], QStringLiteral("img_00.png")), -1);
    QCOMPARE(rowByFileName(views[0], QStringLiteral("img_01.png")), 0);
    QTRY_VERIFY_WITH_TIMEOUT(isRowSelected(views[0], 0), 1000);
    QCOMPARE(marks.markForImage(dir.path(), paths.at(0)), QStringLiteral("B"));
}

void tst_BrowsePanel::categoryFilter_ctrlClickAnchorsSameIndexRowsAcrossColumns()
{
    QTemporaryDir unmarkedDir;
    QTemporaryDir markedDir;
    QVERIFY(unmarkedDir.isValid());
    QVERIFY(markedDir.isValid());

    QImage image(16, 16, QImage::Format_ARGB32);
    image.fill(Qt::darkCyan);
    for (int i = 0; i < 3; ++i) {
        const QString name = QString("img_%1.png").arg(i, 2, 10, QChar('0'));
        QVERIFY(image.save(unmarkedDir.filePath(name)));
        QVERIFY(image.save(markedDir.filePath(name)));
    }

    ImageMarkManager marks;
    QVERIFY(marks.setMarkForImage(markedDir.path(), markedDir.filePath("img_00.png"), "A"));
    QVERIFY(marks.setMarkForImage(markedDir.path(), markedDir.filePath("img_02.png"), "A"));

    CompareSession session;
    ImageLoader loader;
    BrowsePanel panel(&session, &loader);
    panel.setImageMarkManager(&marks);
    panel.resize(900, 500);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    QVERIFY(session.addFolder(unmarkedDir.path()));
    QVERIFY(session.addFolder(markedDir.path()));

    QList<QListView *> views;
    QTRY_VERIFY_WITH_TIMEOUT((views = sortedViews(panel), views.size() == 2), 5000);
    waitForRows(views[0], 3);
    waitForRows(views[1], 3);

    auto *categoryFilter = panel.findChild<QComboBox *>("categoryFilterComboBox");
    QVERIFY(categoryFilter != nullptr);
    const int categoryAIndex = categoryFilter->findData(QStringLiteral("A"));
    QVERIFY(categoryAIndex >= 0);
    categoryFilter->setCurrentIndex(categoryAIndex);

    QTRY_COMPARE_WITH_TIMEOUT(views[0]->model()->rowCount(), 0, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(views[1]->model()->rowCount(), 2, 1000);

    const int anchorRow = rowByFileName(views[1], QStringLiteral("img_00.png"));
    QVERIFY(anchorRow >= 0);
    clickRow(views[1], anchorRow, Qt::ControlModifier);

    QTRY_COMPARE_WITH_TIMEOUT(views[0]->model()->rowCount(), 2, 1000);
    QCOMPARE(views[1]->model()->rowCount(), 2);
    QVERIFY(rowByFileName(views[0], QStringLiteral("img_00.png")) >= 0);
    QVERIFY(rowByFileName(views[0], QStringLiteral("img_02.png")) >= 0);
    QCOMPARE(rowByFileName(views[0], QStringLiteral("img_01.png")), -1);
}

void tst_BrowsePanel::categoryFilter_altClickAnchorsFuzzyFileNameMatchesAcrossColumns()
{
    QTemporaryDir targetDir;
    QTemporaryDir markedDir;
    QVERIFY(targetDir.isValid());
    QVERIFY(markedDir.isValid());

    QImage image(16, 16, QImage::Format_ARGB32);
    image.fill(Qt::darkMagenta);
    QVERIFY(image.save(targetDir.filePath("cats.png")));
    QVERIFY(image.save(targetDir.filePath("dog.png")));
    QVERIFY(image.save(markedDir.filePath("bird.png")));
    QVERIFY(image.save(markedDir.filePath("cat.png")));

    ImageMarkManager marks;
    QVERIFY(marks.setMarkForImage(markedDir.path(), markedDir.filePath("cat.png"), "A"));

    CompareSession session;
    ImageLoader loader;
    BrowsePanel panel(&session, &loader);
    panel.setImageMarkManager(&marks);
    panel.setFuzzyFileNameMatchEnabled(true);
    panel.resize(900, 500);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    QVERIFY(session.addFolder(targetDir.path()));
    QVERIFY(session.addFolder(markedDir.path()));

    QList<QListView *> views;
    QTRY_VERIFY_WITH_TIMEOUT((views = sortedViews(panel), views.size() == 2), 5000);
    waitForRows(views[0], 2);
    waitForRows(views[1], 2);

    auto *categoryFilter = panel.findChild<QComboBox *>("categoryFilterComboBox");
    QVERIFY(categoryFilter != nullptr);
    const int categoryAIndex = categoryFilter->findData(QStringLiteral("A"));
    QVERIFY(categoryAIndex >= 0);
    categoryFilter->setCurrentIndex(categoryAIndex);

    QTRY_COMPARE_WITH_TIMEOUT(views[0]->model()->rowCount(), 0, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(views[1]->model()->rowCount(), 1, 1000);

    const int anchorRow = rowByFileName(views[1], QStringLiteral("cat.png"));
    QVERIFY(anchorRow >= 0);
    clickRow(views[1], anchorRow, Qt::AltModifier);

    QTRY_COMPARE_WITH_TIMEOUT(views[0]->model()->rowCount(), 1, 1000);
    QCOMPARE(rowByFileName(views[0], QStringLiteral("cats.png")), 0);
    QCOMPARE(rowByFileName(views[0], QStringLiteral("dog.png")), -1);
}

void tst_BrowsePanel::categoryFilter_fuzzyLargeListUsesBoundedIndexAndPreservesTies()
{
    QTemporaryDir targetDir;
    QTemporaryDir markedDir;
    QVERIFY(targetDir.isValid());
    QVERIFY(markedDir.isValid());

    // This used to run one full Levenshtein DP for every marked-anchor x target
    // pair on the GUI thread. Metadata-only files keep this regression focused on
    // filename matching rather than image encoding/decoding.
    constexpr int kFileCount = 5000;
    QJsonObject marksJson;
    for (int i = 0; i < kFileCount; ++i) {
        const QString suffix = QStringLiteral("%1.jpg")
                                   .arg(i, 5, 10, QLatin1Char('0'));
        const QString anchorName = QStringLiteral("frame_") + suffix;
        const QString targetName = QStringLiteral("flame_") + suffix;

        QFile anchorFile(markedDir.filePath(anchorName));
        QVERIFY(anchorFile.open(QIODevice::WriteOnly));
        anchorFile.close();
        QFile targetFile(targetDir.filePath(targetName));
        QVERIFY(targetFile.open(QIODevice::WriteOnly));
        targetFile.close();

        if ((i % 2) == 0) {
            QJsonObject mark;
            mark.insert(QStringLiteral("category"), QStringLiteral("A"));
            mark.insert(QStringLiteral("timestamp"), i + 1);
            marksJson.insert(anchorName, mark);
        }
    }

    // Both target names are one edit from tie.jpg. Legacy matching chooses the
    // first source row on equal distance; sorted source order makes tae.jpg win.
    for (const QString &name : {QStringLiteral("tae.jpg"), QStringLiteral("tbe.jpg")}) {
        QFile file(targetDir.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
    }
    QFile tieAnchor(markedDir.filePath(QStringLiteral("tie.jpg")));
    QVERIFY(tieAnchor.open(QIODevice::WriteOnly));
    tieAnchor.close();
    QJsonObject tieMark;
    tieMark.insert(QStringLiteral("category"), QStringLiteral("A"));
    tieMark.insert(QStringLiteral("timestamp"), kFileCount + 1);
    marksJson.insert(QStringLiteral("tie.jpg"), tieMark);

    QJsonObject root;
    root.insert(QStringLiteral("version"), 2);
    root.insert(QStringLiteral("marks"), marksJson);
    QFile markFile(markedDir.filePath(QStringLiteral(".imagecompare_marks.json")));
    QVERIFY(markFile.open(QIODevice::WriteOnly));
    QVERIFY(markFile.write(QJsonDocument(root).toJson(QJsonDocument::Compact)) > 0);
    markFile.close();

    ImageMarkManager marks;
    CompareSession session;
    ImageLoader loader;
    BrowsePanel panel(&session, &loader);
    panel.setImageMarkManager(&marks);
    panel.setFuzzyFileNameMatchEnabled(true);
    panel.resize(900, 500);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    QVERIFY(session.addFolder(targetDir.path()));
    QVERIFY(session.addFolder(markedDir.path()));

    QList<QListView *> views;
    QTRY_VERIFY_WITH_TIMEOUT((views = sortedViews(panel), views.size() == 2), 5000);
    waitForRows(views[0], kFileCount + 2);
    waitForRows(views[1], kFileCount + 1);

    auto *categoryFilter = panel.findChild<QComboBox *>("categoryFilterComboBox");
    QVERIFY(categoryFilter != nullptr);
    const int categoryAIndex = categoryFilter->findData(QStringLiteral("A"));
    QVERIFY(categoryAIndex >= 0);
    categoryFilter->setCurrentIndex(categoryAIndex);
    QTRY_COMPARE_WITH_TIMEOUT(views[0]->model()->rowCount(), 0, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(views[1]->model()->rowCount(), kFileCount / 2 + 1, 2000);

    const int tieRow = rowByFileName(views[1], QStringLiteral("tie.jpg"));
    QVERIFY(tieRow >= 0);
    const QModelIndex tieIndex = views[1]->model()->index(tieRow, 0);
    views[1]->scrollTo(tieIndex, QAbstractItemView::PositionAtCenter);
    QTest::qWait(30);
    const QRect tieRect = views[1]->visualRect(tieIndex);
    QVERIFY(tieRect.isValid());
    QElapsedTimer timer;
    timer.start();
    QTest::mouseClick(views[1]->viewport(), Qt::LeftButton,
                      Qt::AltModifier, tieRect.center());
    const qint64 elapsedMs = timer.elapsed();

    QTRY_COMPARE_WITH_TIMEOUT(views[0]->model()->rowCount(), kFileCount / 2 + 1, 2000);
    QCOMPARE(rowByFileName(views[0], QStringLiteral("tae.jpg")) >= 0, true);
    QCOMPARE(rowByFileName(views[0], QStringLiteral("tbe.jpg")), -1);
    QVERIFY(isRowSelected(views[0], rowByFileName(views[0], QStringLiteral("tae.jpg"))));
    QVERIFY2(elapsedMs < 500,
             qPrintable(QStringLiteral("large fuzzy filter blocked GUI for %1 ms")
                            .arg(elapsedMs)));
    qDebug() << "[Browse fuzzy index]" << (kFileCount / 2 + 1)
             << "anchors x" << (kFileCount + 2) << "targets in"
             << elapsedMs << "ms";
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

    const QList<int> expectedRows = {1, 2, 3, 5, 6, 7};
    QTRY_VERIFY_WITH_TIMEOUT(std::all_of(expectedRows.cbegin(), expectedRows.cend(),
                                        [&loader, &imagePaths](int row) {
        return !loader.getCachedImage(imagePaths.at(row)).isNull();
    }), 5000);

    // Neighbor prefetch is deliberately silent: warming the cache must not
    // drive ComparePanel as though six new visible selections arrived.
    QCOMPARE(imageSpy.count(), 0);
}

void tst_BrowsePanel::altMatchedNavigation_advancesAnchorAndRematchesOtherFolders()
{
    QTemporaryDir dirA;
    QTemporaryDir dirB;
    QVERIFY(dirA.isValid());
    QVERIFY(dirB.isValid());

    QImage image(16, 16, QImage::Format_ARGB32);
    image.fill(Qt::cyan);
    QVERIFY(image.save(dirA.filePath("img_00.png")));
    QVERIFY(image.save(dirA.filePath("img_01.png")));
    QVERIFY(image.save(dirA.filePath("img_02.png")));
    QVERIFY(image.save(dirB.filePath("img_00.png")));
    QVERIFY(image.save(dirB.filePath("img_02.png")));

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
    waitForRows(views[0], 3);
    waitForRows(views[1], 2);

    const int a0 = rowByFileName(views[0], QStringLiteral("img_00.png"));
    const int a1 = rowByFileName(views[0], QStringLiteral("img_01.png"));
    const int a2 = rowByFileName(views[0], QStringLiteral("img_02.png"));
    const int b0 = rowByFileName(views[1], QStringLiteral("img_00.png"));
    const int b2 = rowByFileName(views[1], QStringLiteral("img_02.png"));
    QVERIFY(a0 >= 0 && a1 >= 0 && a2 >= 0);
    QVERIFY(b0 >= 0 && b2 >= 0);

    clickRow(views[0], a0, Qt::AltModifier);
    QTRY_VERIFY_WITH_TIMEOUT(isRowSelected(views[0], a0), 1000);
    QVERIFY(isRowSelected(views[1], b0));

    panel.navigateNext();
    QTRY_VERIFY_WITH_TIMEOUT(isRowSelected(views[0], a1), 1000);
    QVERIFY(!isRowSelected(views[1], b0));
    QVERIFY(!isRowSelected(views[1], b2));

    panel.navigateNext();
    QTRY_VERIFY_WITH_TIMEOUT(isRowSelected(views[0], a2), 1000);
    QVERIFY(isRowSelected(views[1], b2));
}

void tst_BrowsePanel::independentNavigation_movesEachSelectedFolderSeparately()
{
    QTemporaryDir dirA;
    QTemporaryDir dirB;
    QVERIFY(dirA.isValid());
    QVERIFY(dirB.isValid());

    QImage image(16, 16, QImage::Format_ARGB32);
    image.fill(Qt::magenta);
    QVERIFY(image.save(dirA.filePath("img_00.png")));
    QVERIFY(image.save(dirA.filePath("img_01.png")));
    QVERIFY(image.save(dirA.filePath("img_02.png")));
    QVERIFY(image.save(dirB.filePath("img_00.png")));
    QVERIFY(image.save(dirB.filePath("img_02.png")));

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
    waitForRows(views[0], 3);
    waitForRows(views[1], 2);

    const int a0 = rowByFileName(views[0], QStringLiteral("img_00.png"));
    const int a1 = rowByFileName(views[0], QStringLiteral("img_01.png"));
    const int b0 = rowByFileName(views[1], QStringLiteral("img_00.png"));
    const int b2 = rowByFileName(views[1], QStringLiteral("img_02.png"));
    QVERIFY(a0 >= 0 && a1 >= 0);
    QVERIFY(b0 >= 0 && b2 >= 0);

    clickRow(views[0], a0);
    clickRow(views[1], b0);
    QTRY_VERIFY_WITH_TIMEOUT(isRowSelected(views[0], a0), 1000);
    QVERIFY(isRowSelected(views[1], b0));

    panel.navigateNext();
    QTRY_VERIFY_WITH_TIMEOUT(isRowSelected(views[0], a1), 1000);
    QVERIFY(isRowSelected(views[1], b2));
}

void tst_BrowsePanel::folderSwap_reordersColumnsAndKeepsSelections()
{
    QTemporaryDir dirA;
    QTemporaryDir dirB;
    QVERIFY(dirA.isValid());
    QVERIFY(dirB.isValid());

    QImage imageA(16, 16, QImage::Format_ARGB32);
    imageA.fill(Qt::red);
    const QString imageAPath = dirA.filePath("a.png");
    QVERIFY(imageA.save(imageAPath));

    QImage imageB(16, 16, QImage::Format_ARGB32);
    imageB.fill(Qt::blue);
    const QString imageBPath = dirB.filePath("b.png");
    QVERIFY(imageB.save(imageBPath));

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
    waitForRows(views[0], 1);
    waitForRows(views[1], 1);
    QCOMPARE(folderPathForView(views[0]), dirA.path());
    QCOMPARE(folderPathForView(views[1]), dirB.path());

    clickRow(views[0], 0);
    QTRY_VERIFY_WITH_TIMEOUT(isRowSelected(views[0], 0), 1000);

    QVERIFY(session.swapFolders(0, 1));
    QCoreApplication::processEvents();

    views = sortedViews(panel);
    QCOMPARE(views.size(), 2);
    QCOMPARE(folderPathForView(views[0]), dirB.path());
    QCOMPARE(folderPathForView(views[1]), dirA.path());
    QVERIFY(!isRowSelected(views[0], 0));
    QVERIFY(isRowSelected(views[1], 0));

    Q_UNUSED(imageAPath);
    Q_UNUSED(imageBPath);
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

void tst_BrowsePanel::thumbnailSize_scalesWithPanelWidthAndPushesDecodeBucket()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    for (int i = 0; i < 4; ++i) {
        QImage image(64, 48, QImage::Format_ARGB32);
        image.fill(QColor::fromHsv((i * 70) % 360, 200, 220));
        QVERIFY(image.save(dir.filePath(QString("img_%1.png").arg(i))));
    }

    CompareSession session;
    ImageLoader loader;
    BrowsePanel panel(&session, &loader);
    // The panel is resizable now (not locked to a fixed width).
    QVERIFY(panel.maximumWidth() > panel.minimumWidth());

    panel.resize(360, 600);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    QVERIFY(session.addFolder(dir.path()));
    QList<QListView *> views;
    QTRY_VERIFY_WITH_TIMEOUT((views = sortedViews(panel), views.size() == 1), 5000);
    waitForRows(views[0], 4);

    const int narrowImageWidth = panel.thumbnailMetrics().imageWidth;
    const int narrowItemHeight = panel.thumbnailMetrics().itemHeight;

    // Widen the panel: thumbnails grow to fill the wider column.
    panel.resize(760, 600);
    QTRY_VERIFY_WITH_TIMEOUT(panel.thumbnailMetrics().imageWidth > narrowImageWidth, 2000);
    const BrowsePanel::ThumbMetrics wide = panel.thumbnailMetrics();
    QVERIFY(wide.itemHeight > narrowItemHeight);

    // The delegate's sizeHint reflects the new size, so the list rows grow too.
    const QModelIndex idx = views[0]->model()->index(0, 0);
    QStyleOptionViewItem opt;
    const QSize hint = views[0]->itemDelegateForIndex(idx)->sizeHint(opt, idx);
    QCOMPARE(hint.height(), wide.itemHeight);
    QCOMPARE(hint.width(), wide.cardWidth);
    QCOMPARE(views[0]->visualRect(idx).height(), wide.itemHeight);

    // After the resize settles, the wider decode bucket is pushed to the model.
    auto *model = qobject_cast<ImageListModel *>(views[0]->model());
    QVERIFY(model != nullptr);
    QTRY_COMPARE_WITH_TIMEOUT(model->thumbnailSize().width(), wide.decodeExtent, 2000);

    // Narrowing shrinks the thumbnails back.
    panel.resize(320, 600);
    QTRY_VERIFY_WITH_TIMEOUT(panel.thumbnailMetrics().imageWidth < wide.imageWidth, 2000);
}

void tst_BrowsePanel::multipleFolders_panelMinimumStaysShrinkable()
{
    // Comparing several folders must NOT lock the browse panel to a large minimum
    // width. A per-column minimum that grew with the folder count previously made
    // the splitter un-draggable and forced the folder panel to collapse. Each
    // added column should add only a small (thumbnail-floor) amount so the panel
    // stays narrow enough to fit alongside the folder and compare panels.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    for (int i = 0; i < 8; ++i) {
        QImage image(48, 48, QImage::Format_ARGB32);
        image.fill(QColor::fromHsv((i * 40) % 360, 200, 220));
        QVERIFY(image.save(dir.filePath(QString("img_%1.png").arg(i))));
    }

    CompareSession session;
    ImageLoader loader;
    BrowsePanel panel(&session, &loader);
    panel.resize(900, 600);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    for (int n = 1; n <= 4; ++n) {
        QVERIFY(session.addFolder(dir.path()));
        QList<QListView *> views;
        QTRY_VERIFY_WITH_TIMEOUT((views = sortedViews(panel), views.size() == n), 5000);
        waitForRows(views[n - 1], 8);
    }

    // Four columns must still fit well under a typical browse-panel width so the
    // splitter stays draggable (folder ~280 + this + compare ~650 < window). The
    // pre-fix regression made each column contribute its full card width, forcing
    // a ~1076px four-folder minimum. The exact post-fix minimum is platform
    // dependent — the filter-toolbar floor and per-column scrollbar/chrome are
    // chunkier on Windows (~330px Linux, ~450px macOS, ~514px Windows) — so assert
    // a generous bound that still sits far below the ~1076px broken regime instead
    // of a tight, macOS-tuned number.
    const int fourFolderMin = panel.minimumSizeHint().width();
    QVERIFY2(fourFolderMin < 760,
             qPrintable(QString("4-folder panel minimum = %1 (expected < 760; "
                                "pre-fix regression was ~1076)").arg(fourFolderMin)));
}

void tst_BrowsePanel::multipleFolders_thumbnailsPaintWithoutResizing_data()
{
    QTest::addColumn<int>("folderCount");
    QTest::addColumn<int>("panelWidth");
    QTest::addColumn<bool>("useFluentStyle");
    QTest::newRow("five-at-default-width") << 5 << 430 << true;
    QTest::newRow("six-at-default-width") << 6 << 430 << true;
    QTest::newRow("seven-at-default-width") << 7 << 430 << true;
    QTest::newRow("eight-at-default-width") << 8 << 430 << true;
    QTest::newRow("eight-at-narrow-width") << 8 << 320 << true;
    QTest::newRow("five-with-platform-scrollbar") << 5 << 430 << false;
    QTest::newRow("eight-with-platform-scrollbar") << 8 << 430 << false;
}

void tst_BrowsePanel::multipleFolders_thumbnailsPaintWithoutResizing()
{
    QFETCH(int, folderCount);
    QFETCH(int, panelWidth);
    QFETCH(bool, useFluentStyle);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QList<QColor> colors;

    CompareSession session;
    ImageLoader loader;
    QWidget styleParent;
    if (useFluentStyle) {
        styleParent.setStyleSheet(FluentStyle::globalStyleSheet());
    }
    BrowsePanel panel(&session, &loader, &styleParent);
    styleParent.resize(panelWidth, 500);
    panel.resize(panelWidth, 500);
    styleParent.show();
    QVERIFY(QTest::qWaitForWindowExposed(&styleParent));

    constexpr int rowCount = 12;
    for (int i = 0; i < folderCount; ++i) {
        const QString name = QStringLiteral("folder_with_a_long_name_%1").arg(i);
        QVERIFY(QDir(dir.path()).mkdir(name));
        const QString path = dir.filePath(name);
        const QColor color = QColor::fromRgb(QColor::fromHsv(i * 47, 220, 210).rgb());
        colors.append(color);
        QImage image(64, 40, QImage::Format_RGB32);
        image.fill(color);
        for (int row = 0; row < rowCount; ++row) {
            QVERIFY(image.save(QDir(path).filePath(QStringLiteral("%1.png").arg(row))));
        }
        QVERIFY(session.addFolder(path));
    }

    const auto views = sortedViews(panel);
    QCOMPARE(views.size(), folderCount);
    for (auto *view : views) {
        waitForRows(view, rowCount);
        QTRY_VERIFY(view->verticalScrollBar()->isVisible());
    }
    QCOMPARE(panel.width(), panelWidth);

    auto *scroll = panel.findChild<QScrollArea *>(QStringLiteral("compareColumnsScrollArea"));
    QVERIFY(scroll != nullptr);
    QWidget *viewport = scroll->viewport();
    QScrollBar *bar = scroll->horizontalScrollBar();
    QCOMPARE(bar->value(), 0);
    const int lastColumnEnd = views.last()->mapTo(scroll->widget(), QPoint()).x()
        + views.last()->width();
    if (folderCount > 6 || lastColumnEnd > viewport->width()) {
        QTRY_VERIFY(bar->maximum() > 0);
    }

    QList<int> offsets{0};
    for (const auto *view : views) {
        const int offset = qBound(0, view->mapTo(viewport, QPoint()).x(), bar->maximum());
        if (offset > offsets.last()) {
            offsets.append(offset);
        }
    }

    QSet<QListView *> paintedColumns;
    for (int offset : std::as_const(offsets)) {
        bar->setValue(offset);
        QList<QPair<QPoint, QColor>> samples;
        for (int i = 0; i < views.size(); ++i) {
            auto *view = views.at(i);
            const QPoint origin = view->viewport()->mapTo(viewport, QPoint());
            const QRect viewRect(origin, view->viewport()->size());
            if (!viewRect.intersects(viewport->rect())) {
                continue;
            }

            const QModelIndex index = view->model()->index(0, 0);
            QTRY_VERIFY_WITH_TIMEOUT(!index.data(ImageListModel::ThumbnailRole).value<QImage>().isNull(), 5000);
            const QRect item = view->visualRect(index);
            const auto metrics = panel.thumbnailMetrics();
            const QRect card = metrics.thumbnailCardRect(item);
            const QRect picture = metrics.thumbnailImageRect(item);
            QVERIFY2(view->viewport()->rect().contains(item),
                     qPrintable(QStringLiteral("Column %1: item ends at %2, viewport width %3")
                                    .arg(i).arg(item.right()).arg(view->viewport()->width())));
            QVERIFY(item.contains(card));
            QVERIFY(card.contains(picture));

            const auto *header = view->parentWidget()->findChild<QLabel *>(
                QStringLiteral("compareColumnHeaderLabel"));
            const auto *progress = view->parentWidget()->findChild<QLabel *>(
                QStringLiteral("compareColumnProgressLabel"));
            QVERIFY(header != nullptr);
            QVERIFY(progress != nullptr);
            QCOMPARE(header->toolTip(), QDir(folderPathForView(view)).dirName());
            QCOMPARE(header->text(), header->toolTip());
            QCOMPARE(progress->text(), progress->toolTip());
            QVERIFY(progress->width() > header->width());

            const QRect visiblePicture = picture.translated(origin).intersected(viewport->rect());
            if (visiblePicture.width() >= 4) {
                samples.append({visiblePicture.center(), colors.at(i)});
                paintedColumns.insert(view);
            }
        }
        QVERIFY(!samples.isEmpty());
        const QPixmap capture = viewport->grab();
        const QImage pixels = capture.toImage();
        for (const auto &[point, expected] : std::as_const(samples)) {
            QCOMPARE(pixels.pixelColor(point * capture.devicePixelRatio()), expected);
        }
    }
    QCOMPARE(paintedColumns.size(), folderCount);

    if (folderCount > 6) {
        const QModelIndex first = views.first()->model()->index(0, 0);
        QTRY_VERIFY(first.data(ImageListModel::ThumbnailRole).value<QImage>().isNull());
        bar->setValue(0);
        QTRY_VERIFY_WITH_TIMEOUT(!first.data(ImageListModel::ThumbnailRole).value<QImage>().isNull(), 5000);
    }
}

void tst_BrowsePanel::moreThanSixFolders_addsAllColumnsWithHorizontalScroll()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    for (int i = 0; i < 4; ++i) {
        QImage image(48, 48, QImage::Format_ARGB32);
        image.fill(QColor::fromHsv((i * 40) % 360, 200, 220));
        QVERIFY(image.save(dir.filePath(QString("img_%1.png").arg(i))));
    }

    CompareSession session;
    ImageLoader loader;
    BrowsePanel panel(&session, &loader);
    panel.resize(720, 500);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    constexpr int folderCount = 8;
    for (int n = 1; n <= folderCount; ++n) {
        QVERIFY(session.addFolder(dir.path()));
    }

    QList<QListView *> views;
    QTRY_VERIFY_WITH_TIMEOUT((views = sortedViews(panel), views.size() == folderCount), 5000);
    waitForRows(views.last(), 4);

    auto *scrollArea = panel.findChild<QScrollArea *>(QStringLiteral("compareColumnsScrollArea"));
    QVERIFY(scrollArea != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(scrollArea->horizontalScrollBar()->maximum() > 0, 2000);

    auto thumbnailAt = [](QListView *view) {
        return view->model()->data(view->model()->index(0, 0),
                                   ImageListModel::ThumbnailRole).value<QImage>();
    };
    QTRY_VERIFY_WITH_TIMEOUT(!thumbnailAt(views.first()).isNull(), 5000);
    QTest::qWait(200);
    QVERIFY2(thumbnailAt(views.last()).isNull(),
             "far duplicate-folder column accepted a shared-loader thumbnail");

    scrollArea->horizontalScrollBar()->setValue(
        scrollArea->horizontalScrollBar()->maximum());
    QTRY_VERIFY_WITH_TIMEOUT(!thumbnailAt(views.last()).isNull(), 5000);
}

QTEST_MAIN(tst_BrowsePanel)
#include "tst_BrowsePanel.moc"
