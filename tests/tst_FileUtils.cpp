#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QImage>
#include <memory>

#include "utils/FileUtils.h"

class tst_FileUtils : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void testIsImageFile();
    void testIsImageFile_nonImage();
    void testIsImageFile_nonExistent();
    void testIsImageFile_caseInsensitive();

    void testScanForImages_empty();
    void testScanForImages_flat();
    void testScanForImages_recursive();
    void testScanForImages_nonRecursive();
    void testScanForImages_nonExistentDir();
    void testScanForImages_sorted();
    void testScanForImagesBatched_batches();
    void testScanForImagesBatched_initialBatchFlushesEarly();
    void testScanForImagesBatched_cancel();

    void testGetSubdirectories();
    void testGetSubdirectories_empty();
    void testGetSubdirectories_nonExistent();

    void testSupportedExtensions();

private:
    QTemporaryDir m_tempDir;

    void createTestFile(const QString &relativePath, bool asImage = false);
    void createTestDir(const QString &relativePath);
};

void tst_FileUtils::initTestCase()
{
    QVERIFY(m_tempDir.isValid());

    // Create test directory structure:
    //   root/
    //     image1.png
    //     image2.jpg
    //     image3.bmp
    //     document.txt
    //     readme.md
    //     subdir/
    //       image4.png
    //       image5.tiff
    //       data.csv
    //       nested/
    //         image6.jpeg

    createTestDir("subdir");
    createTestDir("subdir/nested");
    createTestDir("emptydir");

    createTestFile("image1.png", true);
    createTestFile("image2.jpg", true);
    createTestFile("image3.bmp", true);
    createTestFile("document.txt");
    createTestFile("readme.md");
    createTestFile("subdir/image4.png", true);
    createTestFile("subdir/image5.tiff", true);
    createTestFile("subdir/data.csv");
    createTestFile("subdir/nested/image6.jpeg", true);
}

void tst_FileUtils::createTestFile(const QString &relativePath, bool asImage)
{
    QString fullPath = m_tempDir.filePath(relativePath);
    if (asImage) {
        // Create a small valid image
        QImage img(4, 4, QImage::Format_ARGB32);
        img.fill(Qt::red);
        img.save(fullPath);
    } else {
        QFile file(fullPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("test content");
        file.close();
    }
}

void tst_FileUtils::createTestDir(const QString &relativePath)
{
    QDir dir(m_tempDir.path());
    QVERIFY(dir.mkpath(relativePath));
}

void tst_FileUtils::testIsImageFile()
{
    QVERIFY(FileUtils::isImageFile(m_tempDir.filePath("image1.png")));
    QVERIFY(FileUtils::isImageFile(m_tempDir.filePath("image2.jpg")));
    QVERIFY(FileUtils::isImageFile(m_tempDir.filePath("image3.bmp")));
}

void tst_FileUtils::testIsImageFile_nonImage()
{
    QVERIFY(!FileUtils::isImageFile(m_tempDir.filePath("document.txt")));
    QVERIFY(!FileUtils::isImageFile(m_tempDir.filePath("readme.md")));
}

void tst_FileUtils::testIsImageFile_nonExistent()
{
    QVERIFY(!FileUtils::isImageFile(m_tempDir.filePath("nonexistent.png")));
}

void tst_FileUtils::testIsImageFile_caseInsensitive()
{
    // Create a file with uppercase extension
    QString path = m_tempDir.filePath("UPPER.PNG");
    QImage img(4, 4, QImage::Format_ARGB32);
    img.fill(Qt::blue);
    img.save(path);

    QVERIFY(FileUtils::isImageFile(path));
}

void tst_FileUtils::testScanForImages_empty()
{
    QStringList result = FileUtils::scanForImages(m_tempDir.filePath("emptydir"));
    QVERIFY(result.isEmpty());
}

void tst_FileUtils::testScanForImages_flat()
{
    QStringList result = FileUtils::scanForImages(m_tempDir.path(), false);
    // Should find image1.png, image2.jpg, image3.bmp (and possibly UPPER.PNG from earlier test)
    QVERIFY(result.size() >= 3);

    // Should NOT contain subdir images
    for (const QString &path : result) {
        QVERIFY(!path.contains("subdir"));
    }
}

void tst_FileUtils::testScanForImages_recursive()
{
    QStringList result = FileUtils::scanForImages(m_tempDir.path(), true);
    // Should find all image files including subdirectories
    QVERIFY(result.size() >= 6);

    // Should contain the nested image
    bool foundNested = false;
    for (const QString &path : result) {
        if (path.contains("nested") && path.contains("image6")) {
            foundNested = true;
            break;
        }
    }
    QVERIFY(foundNested);
}

void tst_FileUtils::testScanForImages_nonRecursive()
{
    QStringList result = FileUtils::scanForImages(m_tempDir.path(), false);
    for (const QString &path : result) {
        QVERIFY(!path.contains("subdir"));
    }
}

void tst_FileUtils::testScanForImages_nonExistentDir()
{
    QStringList result = FileUtils::scanForImages("/nonexistent/path/12345");
    QVERIFY(result.isEmpty());
}

void tst_FileUtils::testScanForImages_sorted()
{
    QStringList result = FileUtils::scanForImages(m_tempDir.path(), true);
    QStringList sorted = result;
    std::sort(sorted.begin(), sorted.end());
    QCOMPARE(result, sorted);
}

void tst_FileUtils::testScanForImagesBatched_batches()
{
    QStringList all;
    int progressUpdates = 0;
    FileUtils::ScanOptions options;
    options.recursive = true;
    options.batchSize = 2;
    options.initialBatchSize = 1;

    FileUtils::scanForImagesBatched(
        m_tempDir.path(),
        options,
        [&all](const QStringList &batch, bool /*initialBatch*/) {
            all.append(batch);
        },
        [&progressUpdates](const FileUtils::ScanProgress &progress) {
            ++progressUpdates;
            if (progress.finished) {
                QVERIFY(progress.discoveredCount >= 6);
            }
        });

    QVERIFY(all.size() >= 6);
    QVERIFY(progressUpdates > 0);
}

void tst_FileUtils::testScanForImagesBatched_initialBatchFlushesEarly()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    for (int i = 0; i < 20; ++i) {
        QImage img(4, 4, QImage::Format_ARGB32);
        img.fill(Qt::green);
        QVERIFY(img.save(dir.filePath(QString("image_%1.png").arg(i, 2, 10, QLatin1Char('0')))));
    }

    FileUtils::ScanOptions options;
    options.recursive = false;
    options.batchSize = 1000;
    options.initialBatchSize = 5;

    int batchCount = 0;
    bool sawInitialBatch = false;
    bool sawNonFinishedProgress = false;

    FileUtils::scanForImagesBatched(
        dir.path(),
        options,
        [&batchCount, &sawInitialBatch](const QStringList &batch, bool initialBatch) {
            ++batchCount;
            if (initialBatch) {
                sawInitialBatch = true;
                QCOMPARE(batch.size(), 5);
            }
        },
        [&sawNonFinishedProgress](const FileUtils::ScanProgress &progress) {
            if (!progress.finished) {
                sawNonFinishedProgress = true;
            }
        });

    QVERIFY(sawInitialBatch);
    QVERIFY(sawNonFinishedProgress);
    QVERIFY(batchCount >= 2);
}

void tst_FileUtils::testScanForImagesBatched_cancel()
{
    for (int i = 0; i < 60; ++i) {
        createTestFile(QString("cancel_%1.png").arg(i), true);
    }

    QStringList all;
    auto token = std::make_shared<FileUtils::ScanCancelToken>();
    FileUtils::ScanOptions options;
    options.recursive = false;
    options.batchSize = 5;
    options.initialBatchSize = 5;

    bool cancelledEarly = false;
    FileUtils::scanForImagesBatched(
        m_tempDir.path(),
        options,
        [&all, &token, &cancelledEarly](const QStringList &batch, bool /*initialBatch*/) {
            all.append(batch);
            if (all.size() >= 10 && !token->isCancelled()) {
                token->cancel();
                cancelledEarly = true;
            }
        },
        {},
        token);

    QVERIFY(cancelledEarly);
    QVERIFY(all.size() < 60);
}

void tst_FileUtils::testGetSubdirectories()
{
    QStringList result = FileUtils::getSubdirectories(m_tempDir.path());
    QVERIFY(result.size() >= 2); // emptydir and subdir at minimum

    bool foundSubdir = false;
    bool foundEmptydir = false;
    for (const QString &path : result) {
        if (path.endsWith("subdir")) foundSubdir = true;
        if (path.endsWith("emptydir")) foundEmptydir = true;
    }
    QVERIFY(foundSubdir);
    QVERIFY(foundEmptydir);
}

void tst_FileUtils::testGetSubdirectories_empty()
{
    QStringList result = FileUtils::getSubdirectories(m_tempDir.filePath("emptydir"));
    QVERIFY(result.isEmpty());
}

void tst_FileUtils::testGetSubdirectories_nonExistent()
{
    QStringList result = FileUtils::getSubdirectories("/nonexistent/path/12345");
    QVERIFY(result.isEmpty());
}

void tst_FileUtils::testSupportedExtensions()
{
    QStringList exts = FileUtils::supportedImageExtensions();
    QVERIFY(exts.contains("png"));
    QVERIFY(exts.contains("jpg"));
    QVERIFY(exts.contains("jpeg"));
    QVERIFY(exts.contains("bmp"));
    QVERIFY(exts.contains("tiff"));
}

QTEST_MAIN(tst_FileUtils)
#include "tst_FileUtils.moc"
