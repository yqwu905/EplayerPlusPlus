#include <QtTest>

#include <algorithm>
#include <array>
#include <limits>

#include "services/ComparisonImageComposer.h"

using namespace ComparisonImageComposer;

namespace
{
CellItem solidCell(const QColor &color, const QSize &size = QSize(240, 200))
{
    CellItem cell;
    cell.gridName = QStringLiteral("Comparison");
    cell.fileName = QStringLiteral("frame.png");
    cell.image = QImage(size, QImage::Format_ARGB32);
    cell.image.fill(color);
    return cell;
}

bool containsColor(const QImage &image, const QRect &area, const QColor &color, int tolerance = 0)
{
    for (int y = area.top(); y <= area.bottom(); ++y) {
        for (int x = area.left(); x <= area.right(); ++x) {
            const QRgb pixel = image.pixel(x, y);
            if (std::abs(qRed(pixel) - color.red()) <= tolerance
                && std::abs(qGreen(pixel) - color.green()) <= tolerance
                && std::abs(qBlue(pixel) - color.blue()) <= tolerance
                && qAlpha(pixel) == color.alpha()) {
                return true;
            }
        }
    }
    return false;
}
} // namespace

class tst_ComparisonImageComposer : public QObject
{
    Q_OBJECT

private slots:
    void testGridDimensions_data();
    void testGridDimensions();
    void testNoImages();
    void testComposeGrid_data();
    void testComposeGrid();
    void testPlaceholder();
    void testVisibleSourceRect();
    void testInvalidSourceRectUsesFullImage();
    void testSizeLimits();
    void testLongLabels();
};

void tst_ComparisonImageComposer::testGridDimensions_data()
{
    QTest::addColumn<int>("cellCount");
    QTest::addColumn<int>("rows");
    QTest::addColumn<int>("columns");

    QTest::newRow("negative") << -1 << 0 << 0;
    QTest::newRow("empty") << 0 << 0 << 0;
    QTest::newRow("one") << 1 << 1 << 1;
    QTest::newRow("two") << 2 << 1 << 2;
    QTest::newRow("three") << 3 << 1 << 3;
    QTest::newRow("four") << 4 << 2 << 2;
    QTest::newRow("five") << 5 << 2 << 3;
    QTest::newRow("six") << 6 << 2 << 3;
    QTest::newRow("seven") << 7 << 3 << 3;
    QTest::newRow("nine") << 9 << 3 << 3;
    QTest::newRow("large") << std::numeric_limits<int>::max() << 46341 << 46341;
}

void tst_ComparisonImageComposer::testGridDimensions()
{
    QFETCH(int, cellCount);
    QFETCH(int, rows);
    QFETCH(int, columns);

    const GridDimensions grid = gridDimensions(cellCount);
    QCOMPARE(grid.rows, rows);
    QCOMPARE(grid.columns, columns);
}

void tst_ComparisonImageComposer::testNoImages()
{
    QVERIFY(compose({}).isNull());
    QVERIFY(compose({CellItem{}, CellItem{}}).isNull());
}

void tst_ComparisonImageComposer::testComposeGrid_data()
{
    QTest::addColumn<int>("cellCount");
    QTest::addColumn<int>("rows");
    QTest::newRow("two") << 2 << 1;
    QTest::newRow("four") << 4 << 2;
}

void tst_ComparisonImageComposer::testComposeGrid()
{
    QFETCH(int, cellCount);
    QFETCH(int, rows);
    const std::array<QColor, 4> colors{
        QColor(Qt::red), QColor(Qt::green), QColor(Qt::blue), QColor(Qt::magenta),
    };
    const std::array<QColor, 4> badgeColors{
        QColor(0x186FD7), QColor(0x896233), QColor(0xF77313), QColor(0x7455C8),
    };
    QList<CellItem> cells;
    for (int index = 0; index < cellCount; ++index) {
        cells.append(solidCell(colors[index]));
    }
    cells[1].badgeColor = badgeColors[1];

    const QImage result = compose(cells);
    QVERIFY(!result.isNull());
    QCOMPARE(result.format(), QImage::Format_ARGB32);
    QCOMPARE(result.pixelColor(0, 0), QColor(0xF3F6FA));
    const int tileWidth = result.width() / 2;
    const int tileHeight = result.height() / rows;
    for (int index = 0; index < cellCount; ++index) {
        const int left = (index % 2) * tileWidth;
        const int top = (index / 2) * tileHeight;
        QCOMPARE(result.pixelColor(left + tileWidth / 2, top + 2 * tileHeight / 3),
                 colors[index]);
        const QRect header(left, top, tileWidth, tileHeight / 4);
        QVERIFY(containsColor(result, header, badgeColors[index]));
        QVERIFY(containsColor(result, header, QColor(0x1F2937), 20));
        QVERIFY(containsColor(result, header, QColor(0x4B5563), 20));
    }
}

void tst_ComparisonImageComposer::testPlaceholder()
{
    CellItem empty;
    empty.placeholderText = QStringLiteral("Waiting for image");
    const QImage result = compose({solidCell(Qt::red), empty});
    QVERIFY(!result.isNull());
    QCOMPARE(result.pixelColor(result.width() / 4, 2 * result.height() / 3), QColor(Qt::red));

    const QRect emptyImageArea(result.width() / 2, result.height() / 2,
                               result.width() / 2, result.height() / 2);
    QVERIFY(containsColor(result, emptyImageArea, QColor(0xF8FAFC)));
    QVERIFY(containsColor(result, emptyImageArea, QColor(0x9CA3AF), 20));
    QVERIFY(!containsColor(result, emptyImageArea, QColor(Qt::red)));
}

void tst_ComparisonImageComposer::testVisibleSourceRect()
{
    CellItem cell = solidCell(Qt::red, QSize(400, 200));
    for (int y = 0; y < cell.image.height(); ++y) {
        QRgb *pixels = reinterpret_cast<QRgb *>(cell.image.scanLine(y));
        std::fill(pixels + 200, pixels + 400, qRgb(0, 0, 255));
    }
    cell.visibleSourceRect = QRectF(200, 0, 300, 200);

    const QImage result = compose({cell});
    QVERIFY(!result.isNull());
    const QRect imageArea(0, result.height() / 3, result.width(),
                          result.height() - result.height() / 3);
    QCOMPARE(result.pixelColor(result.width() / 2, 2 * result.height() / 3), QColor(Qt::blue));
    QVERIFY(!containsColor(result, imageArea, QColor(Qt::red)));
}

void tst_ComparisonImageComposer::testInvalidSourceRectUsesFullImage()
{
    CellItem cell = solidCell(Qt::red);
    const QImage fullImage = compose({cell});
    QVERIFY(!fullImage.isNull());

    for (const QRectF &crop : {QRectF(20, 0, -10, 30), QRectF(300, 0, 30, 30),
                               QRectF(5, 5, 0.0001, 20)}) {
        cell.visibleSourceRect = crop;
        QCOMPARE(compose({cell}), fullImage);
    }
}

void tst_ComparisonImageComposer::testSizeLimits()
{
    const CellItem cell = solidCell(Qt::red, QSize(1800, 1200));
    Options options;
    options.maxCellImageEdge = 360;
    options.maxCanvasEdge = 0;
    const QImage limited = compose({cell}, options);
    QVERIFY(!limited.isNull());
    options.maxCellImageEdge = 0;
    QCOMPARE(limited.size(), compose({solidCell(Qt::red, QSize(360, 240))}, options).size());
    const QImage originalSize = compose({cell}, options);
    QVERIFY(originalSize.width() >= cell.image.width());
    QVERIFY(originalSize.height() >= cell.image.height());

    options.maxCellImageEdge = 200;
    options.maxCanvasEdge = 0;
    const QImage cappedBelowDefaultMin = compose({cell}, options);
    QVERIFY(!cappedBelowDefaultMin.isNull());
    QVERIFY(cappedBelowDefaultMin.width() < limited.width());

    options.maxCellImageEdge = 0;
    options.maxCanvasEdge = 301;
    const QImage grid = compose({cell, cell, cell, cell}, options);
    QVERIFY(!grid.isNull());
    QVERIFY(grid.width() <= options.maxCanvasEdge);
    QVERIFY(grid.height() <= options.maxCanvasEdge);
    QCOMPARE(grid.pixelColor(grid.width() / 4, grid.height() / 4), QColor(Qt::red));
    QCOMPARE(grid.pixelColor(3 * grid.width() / 4, 3 * grid.height() / 4), QColor(Qt::red));

    options.maxCanvasEdge = 1;
    options.minCellWidth = 0;
    options.minCellImageHeight = -1;
    QVERIFY(compose({cell, cell, cell, cell}, options).isNull());

    // Tall skinny images must retain enough cell width to render both header labels.
    const CellItem tallSkinny = solidCell(Qt::blue, QSize(320, 12800));
    const QImage tallResult = compose({tallSkinny});
    QVERIFY(!tallResult.isNull());
    QVERIFY(tallResult.width() >= Options{}.minCellWidth);
    const QRect tallHeader(0, 0, tallResult.width(), tallResult.height() / 8);
    QVERIFY(containsColor(tallResult, tallHeader, QColor(0x186FD7)));
    QVERIFY(containsColor(tallResult, tallHeader, QColor(0x1F2937), 20));
    QVERIFY(containsColor(tallResult, tallHeader, QColor(0x4B5563), 20));
    QCOMPARE(tallResult.pixelColor(tallResult.width() / 2, tallResult.height() / 2), QColor(Qt::blue));
}

void tst_ComparisonImageComposer::testLongLabels()
{
    CellItem cell = solidCell(Qt::red);
    cell.gridName = QStringLiteral("对比网格名称 Long comparison grid name").repeated(40);
    cell.fileName = QStringLiteral("文件名 Long file name").repeated(80) + QStringLiteral(".png");
    const QImage result = compose({cell});
    QVERIFY(!result.isNull());
    const QRect header(0, 0, result.width(), result.height() / 4);
    QVERIFY(containsColor(result, header, QColor(0x1F2937), 20));
    QVERIFY(containsColor(result, header, QColor(0x4B5563), 20));
    QCOMPARE(result.pixelColor(result.width() / 2, 2 * result.height() / 3), QColor(Qt::red));
}

QTEST_MAIN(tst_ComparisonImageComposer)
#include "tst_ComparisonImageComposer.moc"
