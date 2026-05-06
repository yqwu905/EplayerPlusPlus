#include <QTest>
#include <QToolButton>

#include "app/MainWindow.h"
#include "widgets/ComparePanel.h"

class tst_MainWindow : public QObject
{
    Q_OBJECT

private slots:
    void commandBar_removesDeadMenuAndBrowseButtons();
    void commandBar_compareModeButtonsControlComparePanel();
};

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

QTEST_MAIN(tst_MainWindow)
#include "tst_MainWindow.moc"
