#include <QtTest>
#include <QTemporaryDir>
#include <QImage>
#include <QSignalSpy>

#include "models/ImageListModel.h"

class tst_ImageListModel : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void testInitialState();
    void testSetFolder();
    void testSetFolder_empty();
    void testSetFolder_nonExistent();
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

    void testSelection();
    void testSelection_outOfRange();
    void testClearSelection();
    void testSelectedIndices();
    void testSelectionSignal();

    void testRowCount();
    void testRowCount_withParent();

private:
    QTemporaryDir m_tempDir;
    QString m_testDir;
    QString m_emptyDir;
};

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
}

void tst_ImageListModel::testSetFolder()
{
    ImageListModel model;
    model.setFolder(m_testDir);
    QCOMPARE(model.folderPath(), m_testDir);
    QCOMPARE(model.imageCount(), 3);
}

void tst_ImageListModel::testSetFolder_empty()
{
    ImageListModel model;
    model.setFolder(m_emptyDir);
    QCOMPARE(model.imageCount(), 0);
}

void tst_ImageListModel::testSetFolder_nonExistent()
{
    ImageListModel model;
    model.setFolder("/nonexistent/path");
    QCOMPARE(model.imageCount(), 0);
}

void tst_ImageListModel::testFolderName()
{
    ImageListModel model;
    model.setFolder(m_testDir);
    QCOMPARE(model.folderName(), "images");
}

void tst_ImageListModel::testRefresh()
{
    ImageListModel model;
    model.setFolder(m_testDir);
    int count = model.imageCount();

    // Add a new image
    QImage img(10, 10, QImage::Format_ARGB32);
    img.fill(Qt::blue);
    img.save(m_testDir + "/date.png");

    model.refresh();
    QCOMPARE(model.imageCount(), count + 1);

    // Clean up the extra image
    QFile::remove(m_testDir + "/date.png");
}

void tst_ImageListModel::testImagePathAt()
{
    ImageListModel model;
    model.setFolder(m_testDir);

    // Images are sorted, so apple.png should be first
    QString path = model.imagePathAt(0);
    QVERIFY(path.endsWith("apple.png"));
}

void tst_ImageListModel::testImagePathAt_invalid()
{
    ImageListModel model;
    model.setFolder(m_testDir);
    QVERIFY(model.imagePathAt(-1).isEmpty());
    QVERIFY(model.imagePathAt(999).isEmpty());
}

void tst_ImageListModel::testFileNameAt()
{
    ImageListModel model;
    model.setFolder(m_testDir);
    QCOMPARE(model.fileNameAt(0), "apple.png");
}

void tst_ImageListModel::testFileNameAt_invalid()
{
    ImageListModel model;
    model.setFolder(m_testDir);
    QVERIFY(model.fileNameAt(-1).isEmpty());
    QVERIFY(model.fileNameAt(999).isEmpty());
}

void tst_ImageListModel::testImageCount()
{
    ImageListModel model;
    QCOMPARE(model.imageCount(), 0);
    model.setFolder(m_testDir);
    QCOMPARE(model.imageCount(), 3);
}

void tst_ImageListModel::testIndexOfFileName()
{
    ImageListModel model;
    model.setFolder(m_testDir);
    QCOMPARE(model.indexOfFileName("apple.png"), 0);
    QCOMPARE(model.indexOfFileName("banana.png"), 1);
    QCOMPARE(model.indexOfFileName("cherry.png"), 2);
}

void tst_ImageListModel::testIndexOfFileName_notFound()
{
    ImageListModel model;
    model.setFolder(m_testDir);
    QCOMPARE(model.indexOfFileName("nonexistent.png"), -1);
}

void tst_ImageListModel::testDataDisplayRole()
{
    ImageListModel model;
    model.setFolder(m_testDir);
    QVariant data = model.data(model.index(0), Qt::DisplayRole);
    QCOMPARE(data.toString(), "apple.png");
}

void tst_ImageListModel::testDataFilePathRole()
{
    ImageListModel model;
    model.setFolder(m_testDir);
    QVariant data = model.data(model.index(0), ImageListModel::FilePathRole);
    QVERIFY(data.toString().endsWith("apple.png"));
    QVERIFY(QDir::isAbsolutePath(data.toString())); // absolute path
}

void tst_ImageListModel::testDataFileNameRole()
{
    ImageListModel model;
    model.setFolder(m_testDir);
    QVariant data = model.data(model.index(1), ImageListModel::FileNameRole);
    QCOMPARE(data.toString(), "banana.png");
}

void tst_ImageListModel::testDataIsSelectedRole()
{
    ImageListModel model;
    model.setFolder(m_testDir);

    QVERIFY(!model.data(model.index(0), ImageListModel::IsSelectedRole).toBool());

    model.setSelected(0, true);
    QVERIFY(model.data(model.index(0), ImageListModel::IsSelectedRole).toBool());
}

void tst_ImageListModel::testSelection()
{
    ImageListModel model;
    model.setFolder(m_testDir);

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
    model.setFolder(m_testDir);

    // Should not crash
    model.setSelected(-1, true);
    model.setSelected(999, true);
    QVERIFY(model.selectedIndices().isEmpty());
}

void tst_ImageListModel::testClearSelection()
{
    ImageListModel model;
    model.setFolder(m_testDir);

    model.setSelected(0, true);
    model.setSelected(2, true);
    QCOMPARE(model.selectedIndices().size(), 2);

    model.clearSelection();
    QVERIFY(model.selectedIndices().isEmpty());
}

void tst_ImageListModel::testSelectedIndices()
{
    ImageListModel model;
    model.setFolder(m_testDir);

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
    model.setFolder(m_testDir);

    QSignalSpy spy(&model, &ImageListModel::selectionChanged);

    model.setSelected(0, true);
    QCOMPARE(spy.count(), 1);

    model.setSelected(0, true); // No actual change
    QCOMPARE(spy.count(), 1);

    model.setSelected(0, false);
    QCOMPARE(spy.count(), 2);
}

void tst_ImageListModel::testRowCount()
{
    ImageListModel model;
    QCOMPARE(model.rowCount(), 0);
    model.setFolder(m_testDir);
    QCOMPARE(model.rowCount(), 3);
}

void tst_ImageListModel::testRowCount_withParent()
{
    ImageListModel model;
    model.setFolder(m_testDir);
    // List model should return 0 for any valid parent
    QCOMPARE(model.rowCount(model.index(0)), 0);
}

QTEST_MAIN(tst_ImageListModel)
#include "tst_ImageListModel.moc"
