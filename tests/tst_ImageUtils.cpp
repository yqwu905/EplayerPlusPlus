#include <QtTest>
#include <QTemporaryDir>
#include <QImage>

#include "utils/ImageUtils.h"

class tst_ImageUtils : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void testGenerateThumbnailFromFile();
    void testGenerateThumbnailFromFile_invalidPath();
    void testGenerateThumbnailFromImage();
    void testGenerateThumbnailFromImage_null();
    void testGenerateThumbnailPreservesAspectRatio();

    void testScaleImage();
    void testScaleImage_null();
    void testScaleImage_preservesAspectRatio();

    void testIsValidImage();
    void testIsValidImage_invalidFile();
    void testIsValidImage_nonExistent();

    void testGetImageSize();
    void testGetImageSize_invalidFile();

private:
    QTemporaryDir m_tempDir;
    QString m_testImagePath;
    QString m_testImageWide;
};

void tst_ImageUtils::initTestCase()
{
    QVERIFY(m_tempDir.isValid());

    // Create a 100x100 test image
    m_testImagePath = m_tempDir.filePath("test_100x100.png");
    QImage img(100, 100, QImage::Format_ARGB32);
    img.fill(Qt::red);
    QVERIFY(img.save(m_testImagePath));

    // Create a 200x100 wide test image
    m_testImageWide = m_tempDir.filePath("test_200x100.png");
    QImage wideImg(200, 100, QImage::Format_ARGB32);
    wideImg.fill(Qt::blue);
    QVERIFY(wideImg.save(m_testImageWide));

    // Create a text file (not an image)
    QFile textFile(m_tempDir.filePath("not_image.txt"));
    QVERIFY(textFile.open(QIODevice::WriteOnly));
    textFile.write("not an image");
    textFile.close();
}

void tst_ImageUtils::testGenerateThumbnailFromFile()
{
    QImage thumb = ImageUtils::generateThumbnail(m_testImagePath, QSize(50, 50));
    QVERIFY(!thumb.isNull());
    QVERIFY(thumb.width() <= 50);
    QVERIFY(thumb.height() <= 50);
}

void tst_ImageUtils::testGenerateThumbnailFromFile_invalidPath()
{
    QImage thumb = ImageUtils::generateThumbnail("/nonexistent/path.png", QSize(50, 50));
    QVERIFY(thumb.isNull());
}

void tst_ImageUtils::testGenerateThumbnailFromImage()
{
    QImage source(100, 100, QImage::Format_ARGB32);
    source.fill(Qt::green);

    QImage thumb = ImageUtils::generateThumbnail(source, QSize(50, 50));
    QVERIFY(!thumb.isNull());
    QVERIFY(thumb.width() <= 50);
    QVERIFY(thumb.height() <= 50);
}

void tst_ImageUtils::testGenerateThumbnailFromImage_null()
{
    QImage thumb = ImageUtils::generateThumbnail(QImage(), QSize(50, 50));
    QVERIFY(thumb.isNull());
}

void tst_ImageUtils::testGenerateThumbnailPreservesAspectRatio()
{
    QImage thumb = ImageUtils::generateThumbnail(m_testImageWide, QSize(100, 100));
    QVERIFY(!thumb.isNull());
    // 200x100 scaled to fit 100x100 should be 100x50
    QCOMPARE(thumb.width(), 100);
    QCOMPARE(thumb.height(), 50);
}

void tst_ImageUtils::testScaleImage()
{
    QImage source(100, 100, QImage::Format_ARGB32);
    source.fill(Qt::red);

    QImage scaled = ImageUtils::scaleImage(source, QSize(50, 50));
    QVERIFY(!scaled.isNull());
    QCOMPARE(scaled.width(), 50);
    QCOMPARE(scaled.height(), 50);
}

void tst_ImageUtils::testScaleImage_null()
{
    QImage scaled = ImageUtils::scaleImage(QImage(), QSize(50, 50));
    QVERIFY(scaled.isNull());
}

void tst_ImageUtils::testScaleImage_preservesAspectRatio()
{
    QImage source(200, 100, QImage::Format_ARGB32);
    source.fill(Qt::red);

    QImage scaled = ImageUtils::scaleImage(source, QSize(100, 100));
    QVERIFY(!scaled.isNull());
    QCOMPARE(scaled.width(), 100);
    QCOMPARE(scaled.height(), 50);
}

void tst_ImageUtils::testIsValidImage()
{
    QVERIFY(ImageUtils::isValidImage(m_testImagePath));
}

void tst_ImageUtils::testIsValidImage_invalidFile()
{
    QVERIFY(!ImageUtils::isValidImage(m_tempDir.filePath("not_image.txt")));
}

void tst_ImageUtils::testIsValidImage_nonExistent()
{
    QVERIFY(!ImageUtils::isValidImage("/nonexistent/path.png"));
}

void tst_ImageUtils::testGetImageSize()
{
    QSize size = ImageUtils::getImageSize(m_testImagePath);
    QCOMPARE(size, QSize(100, 100));

    QSize wideSize = ImageUtils::getImageSize(m_testImageWide);
    QCOMPARE(wideSize, QSize(200, 100));
}

void tst_ImageUtils::testGetImageSize_invalidFile()
{
    QSize size = ImageUtils::getImageSize("/nonexistent/path.png");
    QVERIFY(!size.isValid());
}

QTEST_MAIN(tst_ImageUtils)
#include "tst_ImageUtils.moc"
