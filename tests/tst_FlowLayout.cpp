#include <QTest>
#include <QLabel>
#include <QWidget>

#include "widgets/FlowLayout.h"

class tst_FlowLayout : public QObject
{
    Q_OBJECT

private slots:
    void wrapsToMultipleRowsWhenNarrow();
    void singleRowWhenWide();
    void takeAtRemovesItems();
};

// Build a host widget with three fixed-size boxes laid out in a FlowLayout.
static QWidget *makeHost(int boxWidth, int boxHeight)
{
    auto *host = new QWidget;
    auto *layout = new FlowLayout(host, 0, 8, 8);
    for (int i = 0; i < 3; ++i) {
        auto *box = new QLabel(host);
        box->setFixedSize(boxWidth, boxHeight);
        layout->addWidget(box);
    }
    return host;
}

void tst_FlowLayout::wrapsToMultipleRowsWhenNarrow()
{
    QWidget *host = makeHost(100, 20);
    auto *layout = static_cast<FlowLayout *>(host->layout());

    // Only one 100px box fits per 120px row, so three boxes need three rows.
    const int narrowHeight = layout->heightForWidth(120);
    QVERIFY2(narrowHeight >= 3 * 20 + 2 * 8,
             qPrintable(QString("narrowHeight=%1").arg(narrowHeight)));

    // A wider width packs more boxes per row, reducing the total height.
    const int wideHeight = layout->heightForWidth(1000);
    QVERIFY2(wideHeight < narrowHeight,
             qPrintable(QString("wide=%1 narrow=%2").arg(wideHeight).arg(narrowHeight)));

    delete host;
}

void tst_FlowLayout::singleRowWhenWide()
{
    QWidget *host = makeHost(100, 20);
    auto *layout = static_cast<FlowLayout *>(host->layout());

    // 3 * 100 + spacing comfortably fits in 1000px → exactly one row tall.
    QCOMPARE(layout->heightForWidth(1000), 20);

    delete host;
}

void tst_FlowLayout::takeAtRemovesItems()
{
    QWidget *host = makeHost(50, 20);
    auto *layout = static_cast<FlowLayout *>(host->layout());

    QCOMPARE(layout->count(), 3);
    delete layout->takeAt(0);
    QCOMPARE(layout->count(), 2);
    QVERIFY(layout->takeAt(99) == nullptr);

    delete host;
}

QTEST_MAIN(tst_FlowLayout)
#include "tst_FlowLayout.moc"
