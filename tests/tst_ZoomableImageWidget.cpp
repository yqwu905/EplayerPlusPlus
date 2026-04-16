#include <QTest>
#include <QSignalSpy>
#include <QImage>
#include <QPixmap>
#include <QApplication>

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
};

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

    QPointF offset(10.0, -15.0);
    widget.setPanOffset(offset);
    QCOMPARE(widget.panOffset(), offset);
}

void TestZoomableImageWidget::testResetView()
{
    ZoomableImageWidget widget;
    QImage img = createTestImage(100, 100);
    widget.setImage(img);

    widget.setZoomLevel(3.0, QPointF(0.5, 0.5));
    widget.setPanOffset(QPointF(20.0, 30.0));

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

    // Offset beyond half image width/height should be clamped
    widget.setPanOffset(QPointF(1000.0, 1000.0));
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
    widget.setPanOffset(QPointF(5.0, 5.0), true);
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
    widget.setPanOffset(QPointF(5.0, 5.0), false);
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

QTEST_MAIN(TestZoomableImageWidget)
#include "tst_ZoomableImageWidget.moc"
