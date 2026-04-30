#include <QtTest>
#include <QTemporaryDir>
#include <QImage>
#include <QSignalSpy>

#include "models/ImageListModel.h"
#include "services/ImageLoader.h"
#include "services/ImageMarkManager.h"

class tst_ImageListModel : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void testInitialState();
    void testSetFolder();
    void testSetFolder_empty();
    void testSetFolder_nonExistent();
    void testSetFolderAsync();
    void testIsLoading();
    void testFolderName();
    void testRefresh();
    void testImagePathAt();
    void testImagePathAt_invalid();
    void testFileNameAt();
    void testFileNameAt_invalid();
    void testImageCount();
    void testIndexOfFileName();
    void testIndexOfFileName_notFound();

    void testDataDisplayRole();
    void testDataFilePathRole();
    void testDataFileNameRole();
    void testDataIsSelectedRole();
    void testDataMarkRole_loadsExistingJson();

    void testSelection();
    void testSelection_outOfRange();
    void testClearSelection();
    void testSelectedIndices();
    void testSelectionSignal();
    void testSetMarkAt_persistsAndEmitsDataChanged();

    void testRowCount();
    void testRowCount_withParent();

    void testHasMoreToLoad_initial();
    void testScanProgressAndIncrementalInsert();
    void testLoadNextThumbnailBatch();
    void testLoadNextThumbnailBatch_resetsOnSetFolder();
    void testHasMoreToLoad_emptyFolder();

private:
    // Helper: set folder and wait for async scan to complete
    void setFolderAndWait(ImageListModel &model, const QString &folder);

    QTemporaryDir m_tempDir;
    QString m_testDir;
    QString m_emptyDir;
};

void tst_ImageListModel::setFolderAndWait(ImageListModel &model, const QString &folder)
{
    QSignalSpy spy(&model, &ImageListModel::folderReady);
    model.setFolder(folder);
    QVERIFY(spy.wait(5000));
}

void tst_ImageListModel::initTestCase()
{
    QVERIFY(m_tempDir.isValid());

    // Create test images in a directory
    m_testDir = m_tempDir.filePath("images");
    QDir().mkpath(m_testDir);

    m_emptyDir = m_tempDir.filePath("empty");
    QDir().mkpath(m_emptyDir);

    // Create test images: apple.png, banana.png, cherry.png
    QStringList names = {"apple.png", "banana.png", "cherry.png"};
    for (const QString &name : names) {
        QImage img(10, 10, QImage::Format_ARGB32);
        img.fill(Qt::red);
        QVERIFY(img.save(m_testDir + "/" + name));
    }
}

void tst_ImageListModel::testInitialState()
{
    ImageListModel model;
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.imageCount(), 0);
    QVERIFY(model.folderPath().isEmpty());
    QVERIFY(model.selectedIndices().isEmpty());
    QVERIFY(!model.isLoading());
    QVERIFY(!model.hasMoreToLoad());
}

void tst_ImageListModel::testSetFolder()
{
    ImageListModel model;
    setFolderAndWait(model, m_testDir);
    QCOMPARE(model.folderPath(), m_testDir);
    QCOMPARE(model.imageCount(), 3);
    QVERIFY(!model.isLoading());
}

void tst_ImageListModel::testSetFolder_empty()
{
    ImageListModel model;
    setFolderAndWait(model, m_emptyDir);
    QCOMPARE(model.imageCount(), 0);
}

void tst_ImageListModel::testSetFolder_nonExistent()
{
    ImageListModel model;
    setFolderAndWait(model, "/nonexistent/path");
    QCOMPARE(model.imageCount(), 0);
}

void tst_ImageListModel::testSetFolderAsync()
{
    ImageListModel model;
    QSignalSpy spy(&model, &ImageListModel::folderReady);

    model.setFolder(m_testDir);

    // Model should initially have 0 images (scan is async)
    QCOMPARE(model.imageCount(), 0);

    // Wait for folderReady signal
    QVERIFY(spy.wait(5000));
    QCOMPARE(spy.count(), 1);

    // Now images should be available
    QCOMPARE(model.imageCount(), 3);
    QVERIFY(!model.isLoading());
}

void tst_ImageListModel::testIsLoading()
{
    ImageListModel model;
    QVERIFY(!model.isLoading());

    QSignalSpy spy(&model, &ImageListModel::folderReady);
    model.setFolder(m_testDir);

    // Should be loading immediately after setFolder
    QVERIFY(model.isLoading());

    // Wait for completion
    QVERIFY(spy.wait(5000));
    QVERIFY(!model.isLoading());
}

void tst_ImageListModel::testFolderName()
{
    ImageListModel model;
    setFolderAndWait(model, m_testDir);
    QCOMPARE(model.folderName(), "images");
}

void tst_ImageListModel::testRefresh()
{
    ImageListModel model;
    setFolderAndWait(model, m_testDir);
    int count = model.imageCount();

    // Add a new image
    QImage img(10, 10, QImage::Format_ARGB32);
    img.fill(Qt::blue);
    img.save(m_testDir + "/date.png");

    QSignalSpy spy(&model, &ImageListModel::folderReady);
    model.refresh();
    QVERIFY(spy.wait(5000));
    QCOMPARE(model.imageCount(), count + 1);

    // Clean up the extra image
    QFile::remove(m_testDir + "/date.png");
}

void tst_ImageListModel::testImagePathAt()
{
    ImageListModel model;
    setFolderAndWait(model, m_testDir);

    // Images are sorted, so apple.png should be first
    QString path = model.imagePathAt(0);
    QVERIFY(path.endsWith("apple.png"));
}

void tst_ImageListModel::testImagePathAt_invalid()
{
    ImageListModel model;
    setFolderAndWait(model, m_testDir);
    QVERIFY(model.imagePathAt(-1).isEmpty());
    QVERIFY(model.imagePathAt(999).isEmpty());
}

void tst_ImageListModel::testFileNameAt()
{
    ImageListModel model;
    setFolderAndWait(model, m_testDir);
    QCOMPARE(model.fileNameAt(0), "apple.png");
}

void tst_ImageListModel::testFileNameAt_invalid()
{
    ImageListModel model;
    setFolderAndWait(model, m_testDir);
    QVERIFY(model.fileNameAt(-1).isEmpty());
    QVERIFY(model.fileNameAt(999).isEmpty());
}

void tst_ImageListModel::testImageCount()
{
    ImageListModel model;
    QCOMPARE(model.imageCount(), 0);
    setFolderAndWait(model, m_testDir);
    QCOMPARE(model.imageCount(), 3);
}

void tst_ImageListModel::testIndexOfFileName()
{
    ImageListModel model;
    setFolderAndWait(model, m_testDir);
    QCOMPARE(model.indexOfFileName("apple.png"), 0);
    QCOMPARE(model.indexOfFileName("banana.png"), 1);
    QCOMPARE(model.indexOfFileName("cherry.png"), 2);
}

void tst_ImageListModel::testIndexOfFileName_notFound()
{
    ImageListModel model;
    setFolderAndWait(model, m_testDir);
    QCOMPARE(model.indexOfFileName("nonexistent.png"), -1);
}

void tst_ImageListModel::testDataDisplayRole()
{
    ImageListModel model;
    setFolderAndWait(model, m_testDir);
    QVariant data = model.data(model.index(0), Qt::DisplayRole);
    QCOMPARE(data.toString(), "apple.png");
}

void tst_ImageListModel::testDataFilePathRole()
{
    ImageListModel model;
    setFolderAndWait(model, m_testDir);
    QVariant data = model.data(model.index(0), ImageListModel::FilePathRole);
    QVERIFY(data.toString().endsWith("apple.png"));
    QVERIFY(QDir::isAbsolutePath(data.toString())); // absolute path
}

void tst_ImageListModel::testDataFileNameRole()
{
    ImageListModel model;
    setFolderAndWait(model, m_testDir);
    QVariant data = model.data(model.index(1), ImageListModel::FileNameRole);
    QCOMPARE(data.toString(), "banana.png");
}

void tst_ImageListModel::testDataIsSelectedRole()
{
    ImageListModel model;
    setFolderAndWait(model, m_testDir);

    QVERIFY(!model.data(model.index(0), ImageListModel::IsSelectedRole).toBool());

    model.setSelected(0, true);
    QVERIFY(model.data(model.index(0), ImageListModel::IsSelectedRole).toBool());
}

void tst_ImageListModel::testDataMarkRole_loadsExistingJson()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString imagePath = dir.filePath("marked.png");
    QImage img(10, 10, QImage::Format_ARGB32);
    img.fill(Qt::cyan);
    QVERIFY(img.save(imagePath));

    ImageMarkManager writer;
    QVERIFY(writer.setMarkForImage(dir.path(), imagePath, "C"));

    ImageMarkManager reader;
    ImageListModel model;
    model.setImageMarkManager(&reader);
    setFolderAndWait(model, dir.path());

    QCOMPARE(model.imageCount(), 1);
    QCOMPARE(model.markAt(0), QStringLiteral("C"));
    QCOMPARE(model.data(model.index(0), ImageListModel::MarkRole).toString(),
             QStringLiteral("C"));
}

void tst_ImageListModel::testSelection()
{
    ImageListModel model;
    setFolderAndWait(model, m_testDir);

    model.setSelected(0, true);
    QVERIFY(model.isSelected(0));
    QVERIFY(!model.isSelected(1));

    model.setSelected(1, true);
    QVERIFY(model.isSelected(0));
    QVERIFY(model.isSelected(1));

    model.setSelected(0, false);
    QVERIFY(!model.isSelected(0));
    QVERIFY(model.isSelected(1));
}

void tst_ImageListModel::testSelection_outOfRange()
{
    ImageListModel model;
    setFolderAndWait(model, m_testDir);

    // Should not crash
    model.setSelected(-1, true);
    model.setSelected(999, true);
    QVERIFY(model.selectedIndices().isEmpty());
}

void tst_ImageListModel::testClearSelection()
{
    ImageListModel model;
    setFolderAndWait(model, m_testDir);

    model.setSelected(0, true);
    model.setSelected(2, true);
    QCOMPARE(model.selectedIndices().size(), 2);

    model.clearSelection();
    QVERIFY(model.selectedIndices().isEmpty());
}

void tst_ImageListModel::testSelectedIndices()
{
    ImageListModel model;
    setFolderAndWait(model, m_testDir);

    model.setSelected(0, true);
    model.setSelected(2, true);

    QList<int> selected = model.selectedIndices();
    QCOMPARE(selected.size(), 2);
    QVERIFY(selected.contains(0));
    QVERIFY(selected.contains(2));
}

void tst_ImageListModel::testSelectionSignal()
{
    ImageListModel model;
    setFolderAndWait(model, m_testDir);

    QSignalSpy spy(&model, &ImageListModel::selectionChanged);

    model.setSelected(0, true);
    QCOMPARE(spy.count(), 1);

    model.setSelected(0, true); // No actual change
    QCOMPARE(spy.count(), 1);

    model.setSelected(0, false);
    QCOMPARE(spy.count(), 2);
}

void tst_ImageListModel::testSetMarkAt_persistsAndEmitsDataChanged()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString imagePath = dir.filePath("image.png");
    QImage img(10, 10, QImage::Format_ARGB32);
    img.fill(Qt::magenta);
    QVERIFY(img.save(imagePath));

    ImageMarkManager manager;
    ImageListModel model;
    model.setImageMarkManager(&manager);
    setFolderAndWait(model, dir.path());

    QSignalSpy dataSpy(&model, &QAbstractItemModel::dataChanged);
    QVERIFY(model.setMarkAt(0, "D"));
    QCOMPARE(model.markAt(0), QStringLiteral("D"));
    QVERIFY(dataSpy.count() >= 1);

    ImageMarkManager reloaded;
    QVERIFY(reloaded.loadFolder(dir.path()));
    QCOMPARE(reloaded.markForImage(dir.path(), imagePath), QStringLiteral("D"));
}

void tst_ImageListModel::testRowCount()
{
    ImageListModel model;
    QCOMPARE(model.rowCount(), 0);
    setFolderAndWait(model, m_testDir);
    QCOMPARE(model.rowCount(), 3);
}

void tst_ImageListModel::testRowCount_withParent()
{
    ImageListModel model;
    setFolderAndWait(model, m_testDir);
    // List model should return 0 for any valid parent
    QCOMPARE(model.rowCount(model.index(0)), 0);
}

void tst_ImageListModel::testHasMoreToLoad_initial()
{
    ImageListModel model;
    // No folder set — nothing to load
    QVERIFY(!model.hasMoreToLoad());

    // After setting folder and scan completes, should have items to load
    setFolderAndWait(model, m_testDir);
    QVERIFY(model.hasMoreToLoad());
}

void tst_ImageListModel::testScanProgressAndIncrementalInsert()
{
    QTemporaryDir largeDir;
    QVERIFY(largeDir.isValid());

    for (int i = 0; i < 1200; ++i) {
        QImage img(8, 8, QImage::Format_ARGB32);
        img.fill(Qt::red);
        QVERIFY(img.save(largeDir.filePath(QString("img_%1.png").arg(i, 4, 10, QChar('0')))));
    }

    ImageListModel model;
    QSignalSpy readySpy(&model, &ImageListModel::folderReady);
    QSignalSpy insertSpy(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy progressSpy(&model, &ImageListModel::scanProgressChanged);

    model.setFolder(largeDir.path());
    QVERIFY(readySpy.wait(8000));

    QCOMPARE(model.imageCount(), 1200);
    QVERIFY(insertSpy.count() >= 2);
    QCOMPARE(insertSpy.first().at(1).toInt(), 0);
    QCOMPARE(insertSpy.first().at(2).toInt(), 299);
    QVERIFY(progressSpy.count() >= 2);
}

void tst_ImageListModel::testLoadNextThumbnailBatch()
{
    ImageListModel model;
    setFolderAndWait(model, m_testDir);
    QCOMPARE(model.imageCount(), 3);

    // Without an ImageLoader, loadNextThumbnailBatch should return false
    QVERIFY(!model.loadNextThumbnailBatch(2));

    // With an ImageLoader, it should progress through the images
    ImageLoader loader;
    model.setImageLoader(&loader);

    QVERIFY(model.hasMoreToLoad());

    // Load batch of 2 — should return true (1 remaining)
    bool more = model.loadNextThumbnailBatch(2);
    QVERIFY(more);

    // Load next batch of 2 — should load the remaining 1 and return false
    more = model.loadNextThumbnailBatch(2);
    QVERIFY(!more);

    // No more to load
    QVERIFY(!model.hasMoreToLoad());
}

void tst_ImageListModel::testLoadNextThumbnailBatch_resetsOnSetFolder()
{
    ImageListModel model;
    ImageLoader loader;
    model.setImageLoader(&loader);

    setFolderAndWait(model, m_testDir);
    QVERIFY(model.hasMoreToLoad());

    // Load all
    model.loadNextThumbnailBatch(100);
    QVERIFY(!model.hasMoreToLoad());

    // Refresh should reset the load index
    QSignalSpy spy(&model, &ImageListModel::folderReady);
    model.refresh();
    QVERIFY(spy.wait(5000));
    QVERIFY(model.hasMoreToLoad());
}

void tst_ImageListModel::testHasMoreToLoad_emptyFolder()
{
    ImageListModel model;
    setFolderAndWait(model, m_emptyDir);
    QVERIFY(!model.hasMoreToLoad());
    QVERIFY(!model.loadNextThumbnailBatch(6));
}

QTEST_MAIN(tst_ImageListModel)
#include "tst_ImageListModel.moc"
