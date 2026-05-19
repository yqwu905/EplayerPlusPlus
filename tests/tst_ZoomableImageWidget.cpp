#include <QTest>
#include <QSignalSpy>
#include <QImage>
#include <QPixmap>
#include <QApplication>

#include <algorithm>
#include <cmath>

#include "widgets/ZoomableImageWidget.h"

class TestZoomableImageWidget : public QObject
{
    Q_OBJECT

private:
    QImage createTestImage(int width, int height, QColor color = Qt::red) {
        QImage img(width, height, QImage::Format_ARGB32);
        img.fill(color);
        return img;
    }

private slots:
    void testInitialState();
    void testSetImageFromQImage();
    void testSetImageFromQPixmap();
    void testSetText();
    void testSetZoomLevel();
    void testSetPanOffset();
    void testResetView();
    void testZoomLevelClamping();
    void testPanOffsetClamping();
    void testSignalEmission();
    void testNoSignalWhenEmitFalse();
    void testHasImage();
    void testSetZoomLevelAnchorsFocalPoint();
    void testNormalizedPanSyncAcrossDifferentSizes();
};

namespace {

// Mirror of the widget's internal "where does image pixel (nx, ny) land in
// widget coordinates" formula. Kept in the test so a regression in
// ZoomableImageWidget can't silently affect both halves of the assertion.
double computeFitScale(const QSize &widgetSize, const QSize &imageSize) {
    return std::min(double(widgetSize.width()) / imageSize.width(),
                    double(widgetSize.height()) / imageSize.height());
}

QPointF normalizedImageToWidget(const QSize &widgetSize,
                                const QSize &imageSize,
                                double zoom,
                                QPointF panOffsetPixels,
                                QPointF normalizedImage) {
    const double scale = computeFitScale(widgetSize, imageSize) * zoom;
    const double imgW = imageSize.width() * scale;
    const double imgH = imageSize.height() * scale;
    const double ox = (widgetSize.width() - imgW) / 2.0 + panOffsetPixels.x() * scale;
    const double oy = (widgetSize.height() - imgH) / 2.0 + panOffsetPixels.y() * scale;
    return QPointF(ox + normalizedImage.x() * imgW,
                   oy + normalizedImage.y() * imgH);
}

} // namespace

void TestZoomableImageWidget::testInitialState()
{
    ZoomableImageWidget widget;
    QCOMPARE(widget.zoomLevel(), 1.0);
    QCOMPARE(widget.panOffset(), QPointF(0.0, 0.0));
    QVERIFY(!widget.hasImage());
    QVERIFY(widget.image().isNull());
}

void TestZoomableImageWidget::testSetImageFromQImage()
{
    ZoomableImageWidget widget;
    QImage img = createTestImage(100, 100);

    widget.setImage(img);

    QVERIFY(widget.hasImage());
    QCOMPARE(widget.image().size(), QSize(100, 100));
}

void TestZoomableImageWidget::testSetImageFromQPixmap()
{
    ZoomableImageWidget widget;
    QPixmap pixmap(200, 150);
    pixmap.fill(Qt::blue);

    widget.setImage(pixmap);

    QVERIFY(widget.hasImage());
    QCOMPARE(widget.image().size(), QSize(200, 150));
}

void TestZoomableImageWidget::testSetText()
{
    ZoomableImageWidget widget;
    QImage img = createTestImage(100, 100);
    widget.setImage(img);
    QVERIFY(widget.hasImage());

    widget.setText("No image");
    QVERIFY(!widget.hasImage());
}

void TestZoomableImageWidget::testSetZoomLevel()
{
    ZoomableImageWidget widget;
    QImage img = createTestImage(100, 100);
    widget.setImage(img);

    widget.setZoomLevel(2.5, QPointF(0.5, 0.5));
    QCOMPARE(widget.zoomLevel(), 2.5);
}

void TestZoomableImageWidget::testSetPanOffset()
{
    ZoomableImageWidget widget;
    QImage img = createTestImage(100, 100);
    widget.setImage(img);

    // Normalized (0.1, -0.15) on a 100x100 image is 10 / -15 image pixels.
    QPointF normalized(0.1, -0.15);
    widget.setNormalizedPan(normalized);
    QCOMPARE(widget.panOffset(), QPointF(10.0, -15.0));
    QCOMPARE(widget.normalizedPan(), normalized);
}

void TestZoomableImageWidget::testResetView()
{
    ZoomableImageWidget widget;
    QImage img = createTestImage(100, 100);
    widget.setImage(img);

    widget.setZoomLevel(3.0, QPointF(0.5, 0.5));
    widget.setNormalizedPan(QPointF(0.2, 0.3));

    widget.resetView();

    QCOMPARE(widget.zoomLevel(), 1.0);
    QCOMPARE(widget.panOffset(), QPointF(0.0, 0.0));
}

void TestZoomableImageWidget::testZoomLevelClamping()
{
    ZoomableImageWidget widget;
    QImage img = createTestImage(100, 100);
    widget.setImage(img);

    // Below minimum (1.0)
    widget.setZoomLevel(0.5, QPointF(0.5, 0.5));
    QCOMPARE(widget.zoomLevel(), 1.0);

    // Above maximum (50.0)
    widget.setZoomLevel(100.0, QPointF(0.5, 0.5));
    QCOMPARE(widget.zoomLevel(), 50.0);
}

void TestZoomableImageWidget::testPanOffsetClamping()
{
    ZoomableImageWidget widget;
    QImage img = createTestImage(100, 100);
    widget.setImage(img);

    // Normalized pan beyond +/-0.5 should clamp (the underlying pixel
    // offset must stay within +/- half the image dimension).
    widget.setNormalizedPan(QPointF(10.0, 10.0));
    QPointF clamped = widget.panOffset();
    QVERIFY(clamped.x() <= 50.0); // half of image width 100
    QVERIFY(clamped.y() <= 50.0); // half of image height 100
}

void TestZoomableImageWidget::testSignalEmission()
{
    ZoomableImageWidget widget;
    QImage img = createTestImage(100, 100);
    widget.setImage(img);

    // Test zoomChanged signal
    QSignalSpy zoomSpy(&widget, &ZoomableImageWidget::zoomChanged);
    widget.setZoomLevel(2.0, QPointF(0.5, 0.5), true); // emitSignal = true
    QCOMPARE(zoomSpy.count(), 1);
    QCOMPARE(zoomSpy.at(0).at(0).toDouble(), 2.0);

    // Test panChanged signal
    QSignalSpy panSpy(&widget, &ZoomableImageWidget::panChanged);
    widget.setNormalizedPan(QPointF(0.05, 0.05), true);
    QCOMPARE(panSpy.count(), 1);

    // Test viewReset signal
    QSignalSpy resetSpy(&widget, &ZoomableImageWidget::viewReset);
    widget.resetView(true);
    QCOMPARE(resetSpy.count(), 1);
}

void TestZoomableImageWidget::testNoSignalWhenEmitFalse()
{
    ZoomableImageWidget widget;
    QImage img = createTestImage(100, 100);
    widget.setImage(img);

    QSignalSpy zoomSpy(&widget, &ZoomableImageWidget::zoomChanged);
    QSignalSpy panSpy(&widget, &ZoomableImageWidget::panChanged);
    QSignalSpy resetSpy(&widget, &ZoomableImageWidget::viewReset);

    widget.setZoomLevel(2.0, QPointF(0.5, 0.5), false);
    widget.setNormalizedPan(QPointF(0.05, 0.05), false);
    widget.resetView(false);

    QCOMPARE(zoomSpy.count(), 0);
    QCOMPARE(panSpy.count(), 0);
    QCOMPARE(resetSpy.count(), 0);
}

void TestZoomableImageWidget::testHasImage()
{
    ZoomableImageWidget widget;
    QVERIFY(!widget.hasImage());

    widget.setImage(createTestImage(50, 50));
    QVERIFY(widget.hasImage());

    widget.setText("placeholder");
    QVERIFY(!widget.hasImage());

    widget.setImage(QPixmap(30, 30));
    // Note: QPixmap(30, 30) creates a null pixmap on some platforms without display
    // so we just verify the call doesn't crash
}

void TestZoomableImageWidget::testSetZoomLevelAnchorsFocalPoint()
{
    // Regression: setZoomLevel used to Q_UNUSED its focalPoint argument, so
    // a linked cell would zoom about its own centre and the image jumped.
    // After the fix, the image pixel at the focal point must stay at the
    // same widget-space position before and after the zoom.
    ZoomableImageWidget widget;
    widget.resize(400, 300);
    QImage img = createTestImage(800, 600); // image aspect matches widget
    widget.setImage(img);

    // Off-centre focal point so the bug would actually manifest.
    const QPointF normalizedFocal(0.25, 0.75);
    const QSize widgetSize = widget.size();
    const QSize imageSize = img.size();

    const QPointF anchorBefore = normalizedImageToWidget(
        widgetSize, imageSize, widget.zoomLevel(), widget.panOffset(), normalizedFocal);

    widget.setZoomLevel(3.0, normalizedFocal);
    QCOMPARE(widget.zoomLevel(), 3.0);

    const QPointF anchorAfter = normalizedImageToWidget(
        widgetSize, imageSize, widget.zoomLevel(), widget.panOffset(), normalizedFocal);

    // The focal image pixel must land at the same widget pixel (within
    // floating-point tolerance) before and after the zoom.
    QVERIFY2(std::abs(anchorBefore.x() - anchorAfter.x()) < 1e-6,
             qPrintable(QString("x drift: before=%1 after=%2")
                            .arg(anchorBefore.x()).arg(anchorAfter.x())));
    QVERIFY2(std::abs(anchorBefore.y() - anchorAfter.y()) < 1e-6,
             qPrintable(QString("y drift: before=%1 after=%2")
                            .arg(anchorBefore.y()).arg(anchorAfter.y())));

    // Sanity: with a non-centre focal point and zoom > 1, the pan must be
    // non-zero (otherwise the focal pixel would have drifted toward centre).
    QVERIFY(widget.panOffset() != QPointF(0.0, 0.0));
}

void TestZoomableImageWidget::testNormalizedPanSyncAcrossDifferentSizes()
{
    // Regression: ComparePanel used to copy raw panOffset() (image pixels)
    // between cells, which gave inconsistent results when cells had
    // different widget sizes or images of different dimensions. Normalized
    // pan must survive being applied verbatim to a differently-sized cell.
    ZoomableImageWidget cellA;
    cellA.resize(400, 400);
    QImage imgA = createTestImage(200, 200); // small image
    cellA.setImage(imgA);

    ZoomableImageWidget cellB;
    cellB.resize(600, 600);
    QImage imgB = createTestImage(1000, 1000); // 5x larger image, larger cell
    cellB.setImage(imgB);

    // Zoom both cells. The focal point is in normalized image coords so it
    // means the same image-relative location in both cells.
    const QPointF normalizedFocal(0.5, 0.5);
    cellA.setZoomLevel(4.0, normalizedFocal);
    cellB.setZoomLevel(4.0, normalizedFocal);

    // Pan cellA off-centre, then sync the normalized pan to cellB.
    cellA.setNormalizedPan(QPointF(0.1, -0.05));
    cellB.setNormalizedPan(cellA.normalizedPan());

    // Both cells must agree on normalized pan even though raw pan offsets
    // differ (cellA stores ~20px, cellB stores ~100px).
    QCOMPARE(cellA.normalizedPan(), cellB.normalizedPan());
    QVERIFY(cellA.panOffset() != cellB.panOffset());

    // A reference image pixel at normalized (0.3, 0.7) must occupy the same
    // *fraction* of each cell's widget coordinate system. Compute the
    // widget-space position of that pixel in each cell and compare relative
    // offsets from the widget centre.
    const QPointF refImagePoint(0.3, 0.7);
    const QPointF posA = normalizedImageToWidget(
        cellA.size(), imgA.size(), cellA.zoomLevel(), cellA.panOffset(), refImagePoint);
    const QPointF posB = normalizedImageToWidget(
        cellB.size(), imgB.size(), cellB.zoomLevel(), cellB.panOffset(), refImagePoint);

    // Convert each to a fraction of its widget so we can compare across
    // different widget sizes.
    auto fractionOf = [](const QPointF &p, const QSize &s) {
        return QPointF(p.x() / s.width(), p.y() / s.height());
    };
    const QPointF fracA = fractionOf(posA, cellA.size());
    const QPointF fracB = fractionOf(posB, cellB.size());
    QVERIFY2(std::abs(fracA.x() - fracB.x()) < 1e-9,
             qPrintable(QString("widget-fraction x mismatch: A=%1 B=%2")
                            .arg(fracA.x()).arg(fracB.x())));
    QVERIFY2(std::abs(fracA.y() - fracB.y()) < 1e-9,
             qPrintable(QString("widget-fraction y mismatch: A=%1 B=%2")
                            .arg(fracA.y()).arg(fracB.y())));
}

QTEST_MAIN(TestZoomableImageWidget)
#include "tst_ZoomableImageWidget.moc"
