#include <QtTest>
#include <QImage>
#include <QColor>
#include <QFuture>
#include <QFutureWatcher>
#include <QSignalSpy>

#include "services/ImageComparer.h"

class tst_ImageComparer : public QObject
{
    Q_OBJECT

private slots:
    void testIdenticalImages();
    void testIdenticalImages_grayscaleConversion();
    void testCompletelyDifferentImages();
    void testSmallDifferenceBelowThreshold();
    void testSmallDifferenceAboveThreshold();
    void testMixedDifferences();
    void testDifferentSizeImages();
    void testDifferentSizeOutOfBounds();
    void testNullImageA();
    void testNullImageB();
    void testBothNull();
    void testPixelDifference();
    void testPixelDifference_identical();
    void testSingleChannelDifference();
    void testThresholdBoundary();
    void testOverlayPreservesOriginalContent();
    void testDiffIntensityScaling();

    // Integer-math regression: exact pixel values against a hand-computed
    // golden image, covering all four code paths (out-of-bounds, identical/
    // grayscale, below-threshold blue overlay, above-threshold red overlay).
    void testIntegerMathGoldenImage();

    // Async API: the QFuture returned by generateToleranceMapAsync must
    // eventually complete and deliver bit-identical output to the sync version.
    void testAsyncVariantCompletes();
    void testAsyncMatchesSync();

private:
    QImage createSolidImage(int w, int h, QRgb color);
};

QImage tst_ImageComparer::createSolidImage(int w, int h, QRgb color)
{
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(color);
    return img;
}

void tst_ImageComparer::testIdenticalImages()
{
    QImage img = createSolidImage(10, 10, qRgb(128, 128, 128));
    QImage result = ImageComparer::generateToleranceMap(img, img, 10);

    QVERIFY(!result.isNull());
    QCOMPARE(result.size(), img.size());

    // All pixels should be converted to grayscale (R == G == B)
    QRgb pixel = result.pixel(5, 5);
    QCOMPARE(qRed(pixel), qGreen(pixel));
    QCOMPARE(qGreen(pixel), qBlue(pixel));
}

void tst_ImageComparer::testIdenticalImages_grayscaleConversion()
{
    // Verify grayscale uses luminance formula: 0.299*R + 0.587*G + 0.114*B
    QImage imgA = createSolidImage(1, 1, qRgb(200, 100, 50));
    QImage imgB = createSolidImage(1, 1, qRgb(200, 100, 50));

    QImage result = ImageComparer::generateToleranceMap(imgA, imgB, 10);
    QVERIFY(!result.isNull());

    QRgb pixel = result.pixel(0, 0);
    int expectedGray = static_cast<int>(0.299 * 200 + 0.587 * 100 + 0.114 * 50);
    QCOMPARE(qRed(pixel), expectedGray);
    QCOMPARE(qGreen(pixel), expectedGray);
    QCOMPARE(qBlue(pixel), expectedGray);
}

void tst_ImageComparer::testCompletelyDifferentImages()
{
    QImage imgA = createSolidImage(10, 10, qRgb(0, 0, 0));
    QImage imgB = createSolidImage(10, 10, qRgb(255, 255, 255));

    QImage result = ImageComparer::generateToleranceMap(imgA, imgB, 10);

    QVERIFY(!result.isNull());

    // diff = 255 > threshold = 10 → red overlay on white original
    // Red channel should be dominant (high), but original is also blended in
    QRgb pixel = result.pixel(5, 5);
    QVERIFY(qRed(pixel) > qGreen(pixel));
    QVERIFY(qRed(pixel) > qBlue(pixel));
    QVERIFY(qRed(pixel) >= 200); // Strong red component
}

void tst_ImageComparer::testSmallDifferenceBelowThreshold()
{
    QImage imgA = createSolidImage(10, 10, qRgb(100, 100, 100));
    QImage imgB = createSolidImage(10, 10, qRgb(105, 100, 100));

    QImage result = ImageComparer::generateToleranceMap(imgA, imgB, 10);

    QVERIFY(!result.isNull());

    // diff = 5, threshold = 10 → blue overlay blended with original
    QRgb pixel = result.pixel(5, 5);
    QVERIFY(qBlue(pixel) > qRed(pixel));  // Blue should be dominant
    QVERIFY(qBlue(pixel) > qGreen(pixel));
}

void tst_ImageComparer::testSmallDifferenceAboveThreshold()
{
    QImage imgA = createSolidImage(10, 10, qRgb(100, 100, 100));
    QImage imgB = createSolidImage(10, 10, qRgb(115, 100, 100));

    QImage result = ImageComparer::generateToleranceMap(imgA, imgB, 10);

    QVERIFY(!result.isNull());

    // diff = 15, threshold = 10 → red overlay blended with original
    QRgb pixel = result.pixel(5, 5);
    QVERIFY(qRed(pixel) > qGreen(pixel));  // Red should be dominant
    QVERIFY(qRed(pixel) > qBlue(pixel));
}

void tst_ImageComparer::testMixedDifferences()
{
    QImage imgA(3, 1, QImage::Format_ARGB32);
    QImage imgB(3, 1, QImage::Format_ARGB32);

    // Pixel 0: identical
    imgA.setPixel(0, 0, qRgb(100, 100, 100));
    imgB.setPixel(0, 0, qRgb(100, 100, 100));

    // Pixel 1: small diff (below threshold)
    imgA.setPixel(1, 0, qRgb(100, 100, 100));
    imgB.setPixel(1, 0, qRgb(105, 100, 100));

    // Pixel 2: large diff (above threshold)
    imgA.setPixel(2, 0, qRgb(100, 100, 100));
    imgB.setPixel(2, 0, qRgb(200, 100, 100));

    QImage result = ImageComparer::generateToleranceMap(imgA, imgB, 10);
    QVERIFY(!result.isNull());

    // Pixel 0: grayscale (identical → grayscale conversion)
    QRgb p0 = result.pixel(0, 0);
    QCOMPARE(qRed(p0), qGreen(p0));
    QCOMPARE(qGreen(p0), qBlue(p0));

    // Pixel 1: blue-tinted (below threshold)
    QRgb p1 = result.pixel(1, 0);
    QVERIFY(qBlue(p1) > qRed(p1));

    // Pixel 2: red-tinted (above threshold)
    QRgb p2 = result.pixel(2, 0);
    QVERIFY(qRed(p2) > qBlue(p2));
}

void tst_ImageComparer::testDifferentSizeImages()
{
    // imageA larger than imageB — overlapping region is identical, should be grayscale
    QImage imgA = createSolidImage(20, 20, qRgb(100, 100, 100));
    QImage imgB = createSolidImage(10, 10, qRgb(100, 100, 100));

    QImage result = ImageComparer::generateToleranceMap(imgA, imgB, 10);

    QVERIFY(!result.isNull());
    // Output should match imageB's size
    QCOMPARE(result.size(), imgB.size());

    // All pixels are within imageA's bounds and identical → grayscale
    QRgb pixel = result.pixel(5, 5);
    QCOMPARE(qRed(pixel), qGreen(pixel));
    QCOMPARE(qGreen(pixel), qBlue(pixel));
}

void tst_ImageComparer::testDifferentSizeOutOfBounds()
{
    // imageA smaller than imageB — pixels outside imageA should get red overlay
    QImage imgA = createSolidImage(5, 5, qRgb(100, 100, 100));
    QImage imgB = createSolidImage(10, 10, qRgb(100, 100, 100));

    QImage result = ImageComparer::generateToleranceMap(imgA, imgB, 10);

    QVERIFY(!result.isNull());
    QCOMPARE(result.size(), imgB.size());

    // Pixel within overlapping region (identical) → grayscale
    QRgb pixelInside = result.pixel(2, 2);
    QCOMPARE(qRed(pixelInside), qGreen(pixelInside));
    QCOMPARE(qGreen(pixelInside), qBlue(pixelInside));

    // Pixel outside imageA's bounds → red overlay
    QRgb pixelOutside = result.pixel(7, 7);
    QVERIFY(qRed(pixelOutside) > qGreen(pixelOutside));
    QVERIFY(qRed(pixelOutside) > qBlue(pixelOutside));
    QVERIFY(qRed(pixelOutside) >= 150); // Strong red component

    // Pixel at edge: x outside, y inside
    QRgb pixelEdge = result.pixel(7, 2);
    QVERIFY(qRed(pixelEdge) > qGreen(pixelEdge));
    QVERIFY(qRed(pixelEdge) > qBlue(pixelEdge));
}

void tst_ImageComparer::testNullImageA()
{
    QImage imgB = createSolidImage(10, 10, qRgb(100, 100, 100));
    QImage result = ImageComparer::generateToleranceMap(QImage(), imgB, 10);
    QVERIFY(result.isNull());
}

void tst_ImageComparer::testNullImageB()
{
    QImage imgA = createSolidImage(10, 10, qRgb(100, 100, 100));
    QImage result = ImageComparer::generateToleranceMap(imgA, QImage(), 10);
    QVERIFY(result.isNull());
}

void tst_ImageComparer::testBothNull()
{
    QImage result = ImageComparer::generateToleranceMap(QImage(), QImage(), 10);
    QVERIFY(result.isNull());
}

void tst_ImageComparer::testPixelDifference()
{
    QCOMPARE(ImageComparer::pixelDifference(qRgb(0, 0, 0), qRgb(255, 255, 255)), 255);
    QCOMPARE(ImageComparer::pixelDifference(qRgb(100, 200, 50), qRgb(110, 200, 50)), 10);
    QCOMPARE(ImageComparer::pixelDifference(qRgb(100, 200, 50), qRgb(100, 200, 50)), 0);
}

void tst_ImageComparer::testPixelDifference_identical()
{
    QCOMPARE(ImageComparer::pixelDifference(qRgb(128, 128, 128), qRgb(128, 128, 128)), 0);
}

void tst_ImageComparer::testSingleChannelDifference()
{
    // Only red channel differs
    QCOMPARE(ImageComparer::pixelDifference(qRgb(100, 50, 50), qRgb(150, 50, 50)), 50);
    // Only green channel differs
    QCOMPARE(ImageComparer::pixelDifference(qRgb(50, 100, 50), qRgb(50, 150, 50)), 50);
    // Only blue channel differs
    QCOMPARE(ImageComparer::pixelDifference(qRgb(50, 50, 100), qRgb(50, 50, 150)), 50);
}

void tst_ImageComparer::testThresholdBoundary()
{
    QImage imgA = createSolidImage(1, 1, qRgb(100, 100, 100));
    QImage imgB = createSolidImage(1, 1, qRgb(110, 100, 100));

    // diff = 10, threshold = 10 → should be blue-tinted (diff <= threshold)
    QImage result10 = ImageComparer::generateToleranceMap(imgA, imgB, 10);
    QRgb pixel10 = result10.pixel(0, 0);
    QVERIFY(qBlue(pixel10) > qRed(pixel10));

    // diff = 10, threshold = 9 → should be red-tinted (diff > threshold)
    QImage result9 = ImageComparer::generateToleranceMap(imgA, imgB, 9);
    QRgb pixel9 = result9.pixel(0, 0);
    QVERIFY(qRed(pixel9) > qBlue(pixel9));
}

void tst_ImageComparer::testOverlayPreservesOriginalContent()
{
    // Verify that the original image content is still visible through the overlay
    QImage imgA = createSolidImage(10, 10, qRgb(0, 0, 0));
    QImage imgB = createSolidImage(10, 10, qRgb(200, 200, 200));

    QImage result = ImageComparer::generateToleranceMap(imgA, imgB, 10);

    QRgb pixel = result.pixel(5, 5);
    // The original brightness (200) should still be somewhat visible
    // Red overlay on bright original means all channels should be non-zero
    QVERIFY(qRed(pixel) > 0);
    QVERIFY(qGreen(pixel) > 0);
    QVERIFY(qBlue(pixel) > 0);
    // But red should still dominate due to overlay
    QVERIFY(qRed(pixel) > qGreen(pixel));
}

void tst_ImageComparer::testDiffIntensityScaling()
{
    QImage imgA = createSolidImage(10, 10, qRgb(100, 100, 100));

    // Small diff above threshold
    QImage imgB1 = createSolidImage(10, 10, qRgb(120, 100, 100));
    // Large diff above threshold
    QImage imgB2 = createSolidImage(10, 10, qRgb(250, 100, 100));

    QImage result1 = ImageComparer::generateToleranceMap(imgA, imgB1, 10);
    QImage result2 = ImageComparer::generateToleranceMap(imgA, imgB2, 10);

    QRgb p1 = result1.pixel(5, 5);
    QRgb p2 = result2.pixel(5, 5);

    // Larger difference should produce a more intense red overlay
    QVERIFY(qRed(p2) >= qRed(p1));
}

void tst_ImageComparer::testIntegerMathGoldenImage()
{
    // 4x1 hand-computed golden image. Each column exercises one branch of
    // the per-pixel logic. Values were derived from the fixed-point formula:
    //   blend(orig, dst, alphaScaled) = (orig*(1000-alphaScaled) + dst*alphaScaled) / 1000
    // with truncation toward zero on the divisions.
    QImage imgA(4, 1, QImage::Format_ARGB32);  // intentionally narrower than B
    imgA.setPixel(0, 0, qRgb(200, 100,  50));  // identical → grayscale
    imgA.setPixel(1, 0, qRgb(100, 100, 100));  // diff = 10, threshold 15 → blue
    imgA.setPixel(2, 0, qRgb(100, 100, 100));  // diff = 20, threshold 10 → red
    imgA.setPixel(3, 0, qRgb(  0,   0,   0));  // unused

    // imgB is wider — column 4 falls outside imgA and exercises out-of-bounds.
    QImage imgB(5, 1, QImage::Format_ARGB32);
    imgB.setPixel(0, 0, qRgb(200, 100,  50));
    imgB.setPixel(1, 0, qRgb(110, 100, 100));
    imgB.setPixel(2, 0, qRgb(120, 100, 100));
    imgB.setPixel(3, 0, qRgb(  0,   0,   0));
    imgB.setPixel(4, 0, qRgb(200, 100,  50));

    // First, exercise below-threshold using threshold=15.
    QImage resultBlue = ImageComparer::generateToleranceMap(imgA, imgB, 15);
    QVERIFY(!resultBlue.isNull());

    // Pixel 0: identical → grayscale = (299*200+587*100+114*50)/1000 = 124
    QCOMPARE(resultBlue.pixel(0, 0), qRgba(124, 124, 124, 255));

    // Pixel 1: diff = 10, threshold = 15 → blue branch
    // t_scaled = 10*1000/15 = 666; alpha_scaled = 100 + 200*666/1000 = 233
    // r = (110*767 + 60*233)/1000 = 98
    // g = (100*767 + 100*233)/1000 = 100
    // b = (100*767 + 255*233)/1000 = 136
    QCOMPARE(resultBlue.pixel(1, 0), qRgba(98, 100, 136, 255));

    // Pixel 4: out-of-bounds (imgB extends past imgA's width = 4)
    // r = (200*400 + 255*600)/1000 = 233
    // g = (100*400 + 50*600)/1000  = 70
    // b = (50*400 + 50*600)/1000   = 50
    QCOMPARE(resultBlue.pixel(4, 0), qRgba(233, 70, 50, 255));

    // Now run again with threshold=10 to make pixel 2 land in the red branch.
    QImage resultRed = ImageComparer::generateToleranceMap(imgA, imgB, 10);
    QVERIFY(!resultRed.isNull());

    // Pixel 2: diff = 20, threshold = 10 → red branch
    // t_scaled = min(1000, 20*1500/255) = 117; alpha_scaled = 250 + 350*117/1000 = 290
    // r = (120*710 + 255*290)/1000 = 159
    // g = (100*710 + 50*290)/1000  = 85
    // b = (100*710 + 50*290)/1000  = 85
    QCOMPARE(resultRed.pixel(2, 0), qRgba(159, 85, 85, 255));
}

void tst_ImageComparer::testAsyncVariantCompletes()
{
    QImage imgA = createSolidImage(64, 64, qRgb(100, 100, 100));
    QImage imgB = createSolidImage(64, 64, qRgb(200, 100, 100));

    QFuture<QImage> future = ImageComparer::generateToleranceMapAsync(imgA, imgB, 10);

    QFutureWatcher<QImage> watcher;
    QSignalSpy finishedSpy(&watcher, &QFutureWatcher<QImage>::finished);
    watcher.setFuture(future);

    // Cap at a generous timeout — 64x64 is trivial, but CI machines can be slow.
    QVERIFY(finishedSpy.wait(5000));

    const QImage result = future.result();
    QVERIFY(!result.isNull());
    QCOMPARE(result.size(), imgB.size());

    // diff = 100 > threshold = 10 → red overlay should dominate.
    const QRgb pixel = result.pixel(32, 32);
    QVERIFY(qRed(pixel) > qBlue(pixel));
    QVERIFY(qRed(pixel) > qGreen(pixel));
}

void tst_ImageComparer::testAsyncMatchesSync()
{
    // Build a slightly non-trivial pair of images so multiple branches fire.
    QImage imgA(32, 32, QImage::Format_ARGB32);
    QImage imgB(32, 32, QImage::Format_ARGB32);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            imgA.setPixel(x, y, qRgb((x * 8) % 256, (y * 8) % 256, ((x + y) * 4) % 256));
            // imgB is slightly perturbed — some pixels identical, some small diff,
            // some large diff.
            const int delta = ((x + y) % 5) * 10;
            imgB.setPixel(x, y, qRgb(qBound(0, (int)((x * 8) % 256) + delta, 255),
                                      (y * 8) % 256,
                                      ((x + y) * 4) % 256));
        }
    }

    const QImage syncResult = ImageComparer::generateToleranceMap(imgA, imgB, 15);
    const QImage asyncResult =
        ImageComparer::generateToleranceMapAsync(imgA, imgB, 15).result();

    QVERIFY(!syncResult.isNull());
    QVERIFY(!asyncResult.isNull());
    QCOMPARE(syncResult.size(), asyncResult.size());

    // Async wraps the same sync implementation, so the byte-level output must
    // match exactly — this is the safety net for the parallelization being
    // race-free.
    QCOMPARE(syncResult, asyncResult);
}

QTEST_MAIN(tst_ImageComparer)
#include "tst_ImageComparer.moc"
