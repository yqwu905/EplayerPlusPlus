#include <QTest>
#include <QAbstractItemView>
#include <QImage>
#include <QListView>
#include <QTemporaryDir>
#include <QToolButton>
#include <algorithm>

#include "app/MainWindow.h"
#include "models/CompareSession.h"
#include "models/ImageListModel.h"
#include "widgets/BrowsePanel.h"
#include "widgets/ComparePanel.h"

class tst_MainWindow : public QObject
{
    Q_OBJECT

private slots:
    void commandBar_removesDeadMenuAndBrowseButtons();
    void commandBar_compareModeButtonsControlComparePanel();
    void directionKeys_navigateBrowseSelection();

private:
    static QList<QListView *> sortedViews(BrowsePanel &panel);
    static void waitForRows(QListView *view, int rows);
    static void clickRow(QListView *view, int row);
    static bool isRowSelected(QListView *view, int row);
};

QList<QListView *> tst_MainWindow::sortedViews(BrowsePanel &panel)
{
    auto views = panel.findChildren<QListView *>(QStringLiteral("compareColumnListView"));
    std::sort(views.begin(), views.end(), [&panel](QListView *lhs, QListView *rhs) {
        return lhs->mapTo(&panel, QPoint(0, 0)).x() < rhs->mapTo(&panel, QPoint(0, 0)).x();
    });
    return views;
}

void tst_MainWindow::waitForRows(QListView *view, int rows)
{
    QVERIFY(view != nullptr);
    QTRY_COMPARE_WITH_TIMEOUT(view->model()->rowCount(), rows, 8000);
}

void tst_MainWindow::clickRow(QListView *view, int row)
{
    QVERIFY(view != nullptr);
    auto *model = view->model();
    QVERIFY(model != nullptr);
    QVERIFY(row >= 0 && row < model->rowCount());

    const QModelIndex index = model->index(row, 0);
    view->scrollTo(index, QAbstractItemView::PositionAtCenter);
    QTest::qWait(30);

    const QRect rect = view->visualRect(index);
    QVERIFY2(rect.isValid(), qPrintable(QString("invalid visual rect for row %1").arg(row)));
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
}

bool tst_MainWindow::isRowSelected(QListView *view, int row)
{
    auto *model = qobject_cast<ImageListModel *>(view ? view->model() : nullptr);
    if (!model || row < 0 || row >= model->imageCount()) {
        return false;
    }
    return model->isSelected(row);
}

void tst_MainWindow::commandBar_removesDeadMenuAndBrowseButtons()
{
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QStringList commandTexts;
    const auto buttons = window.findChildren<QToolButton *>(
        QStringLiteral("commandButton"));
    for (QToolButton *button : buttons) {
        commandTexts.append(button->text());
    }

    QVERIFY(!commandTexts.contains(QStringLiteral("☰")));
    QVERIFY(!commandTexts.contains(QStringLiteral("▦  浏览")));
    QVERIFY(!commandTexts.contains(QStringLiteral("⚙  设置")));
    QVERIFY(!commandTexts.contains(QStringLiteral("⋮")));
    QVERIFY(commandTexts.contains(QStringLiteral("↑  上一张")));
    QVERIFY(commandTexts.contains(QStringLiteral("↓  下一张")));
}

void tst_MainWindow::commandBar_compareModeButtonsControlComparePanel()
{
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto *comparePanel = window.findChild<ComparePanel *>();
    QVERIFY(comparePanel != nullptr);
    QCOMPARE(comparePanel->compareMode(), ComparePanel::SwapMode);

    QToolButton *toleranceButton = nullptr;
    QToolButton *swapButton = nullptr;
    const auto buttons = window.findChildren<QToolButton *>(
        QStringLiteral("commandButton"));
    for (QToolButton *button : buttons) {
        if (button->text() == QStringLiteral("容差图")) {
            toleranceButton = button;
        } else if (button->text() == QStringLiteral("交换")) {
            swapButton = button;
        }
    }
    QVERIFY(toleranceButton != nullptr);
    QVERIFY(swapButton != nullptr);

    QTest::mouseClick(toleranceButton, Qt::LeftButton);
    QCOMPARE(comparePanel->compareMode(), ComparePanel::ToleranceMode);

    QTest::mouseClick(swapButton, Qt::LeftButton);
    QCOMPARE(comparePanel->compareMode(), ComparePanel::SwapMode);
}

void tst_MainWindow::directionKeys_navigateBrowseSelection()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QImage image(16, 16, QImage::Format_ARGB32);
    image.fill(Qt::yellow);
    QVERIFY(image.save(dir.filePath("img_00.png")));
    QVERIFY(image.save(dir.filePath("img_01.png")));

    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto *session = window.findChild<CompareSession *>();
    auto *browsePanel = window.findChild<BrowsePanel *>();
    QVERIFY(session != nullptr);
    QVERIFY(browsePanel != nullptr);

    QVERIFY(session->addFolder(dir.path()));

    QList<QListView *> views;
    QTRY_VERIFY_WITH_TIMEOUT((views = sortedViews(*browsePanel), views.size() == 1), 5000);
    waitForRows(views[0], 2);

    clickRow(views[0], 0);
    QTRY_VERIFY_WITH_TIMEOUT(isRowSelected(views[0], 0), 1000);

    views[0]->setFocus();
    QTest::keyClick(views[0], Qt::Key_Down);
    QTRY_VERIFY_WITH_TIMEOUT(isRowSelected(views[0], 1), 1000);

    QTest::keyClick(views[0], Qt::Key_Up);
    QTRY_VERIFY_WITH_TIMEOUT(isRowSelected(views[0], 0), 1000);
}

QTEST_MAIN(tst_MainWindow)
#include "tst_MainWindow.moc"
