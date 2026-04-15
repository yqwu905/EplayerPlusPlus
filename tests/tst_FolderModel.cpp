#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QSignalSpy>

#include "models/FolderModel.h"

class tst_FolderModel : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void testInitialState();
    void testAddFolder();
    void testAddFolder_duplicate();
    void testAddFolder_invalid();
    void testRemoveFolder();
    void testRemoveFolder_nonRoot();
    void testClearAll();
    void testRootFolderPaths();
    void testSetRootFolders();
    void testSetRootFolders_filterInvalid();
    void testFilePath();
    void testIsRootFolder();
    void testRefreshFolder();
    void testRefreshAll();
    void testDataDisplayRole();
    void testDataToolTipRole();
    void testDataPathRole();
    void testDataIsRootRole();
    void testColumnCount();
    void testHasChildren();
    void testFetchMore();
    void testFetchMoreAsync();
    void testFetchMoreNoDuplicate();
    void testIndex();
    void testParent();

private:
    /**
     * @brief Helper: call fetchMore and wait for the fetchFinished signal.
     * @return true if fetchFinished was received within timeout.
     */
    bool fetchMoreAndWait(FolderModel &model, const QModelIndex &index, int timeoutMs = 5000);

    QTemporaryDir m_tempDir;
    QString m_testDir1;
    QString m_testDir2;
    QString m_testDir3;
};

void tst_FolderModel::initTestCase()
{
    QVERIFY(m_tempDir.isValid());

    // Create test directory structure:
    //   dir1/
    //     subA/
    //     subB/
    //   dir2/
    //     subC/
    //   dir3/
    //     (empty)

    QDir base(m_tempDir.path());

    m_testDir1 = m_tempDir.filePath("dir1");
    m_testDir2 = m_tempDir.filePath("dir2");
    m_testDir3 = m_tempDir.filePath("dir3");

    QVERIFY(base.mkpath("dir1/subA"));
    QVERIFY(base.mkpath("dir1/subB"));
    QVERIFY(base.mkpath("dir2/subC"));
    QVERIFY(base.mkpath("dir3"));
}

void tst_FolderModel::init()
{
    // Each test starts with a fresh model — no need to reset shared state
}

bool tst_FolderModel::fetchMoreAndWait(FolderModel &model, const QModelIndex &index, int timeoutMs)
{
    QSignalSpy spy(&model, &FolderModel::fetchFinished);
    model.fetchMore(index);
    return spy.wait(timeoutMs);
}

void tst_FolderModel::testInitialState()
{
    FolderModel model;
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.columnCount(), 1);
    QVERIFY(model.rootFolderPaths().isEmpty());
}

void tst_FolderModel::testAddFolder()
{
    FolderModel model;
    QVERIFY(model.addFolder(m_testDir1));
    QCOMPARE(model.rowCount(), 1);

    QVERIFY(model.addFolder(m_testDir2));
    QCOMPARE(model.rowCount(), 2);
}

void tst_FolderModel::testAddFolder_duplicate()
{
    FolderModel model;
    QVERIFY(model.addFolder(m_testDir1));
    QVERIFY(!model.addFolder(m_testDir1)); // duplicate
    QCOMPARE(model.rowCount(), 1);
}

void tst_FolderModel::testAddFolder_invalid()
{
    FolderModel model;
    QVERIFY(!model.addFolder("/nonexistent/path/12345"));
    QCOMPARE(model.rowCount(), 0);
}

void tst_FolderModel::testRemoveFolder()
{
    FolderModel model;
    model.addFolder(m_testDir1);
    model.addFolder(m_testDir2);
    QCOMPARE(model.rowCount(), 2);

    QModelIndex idx = model.index(0, 0);
    QVERIFY(model.removeFolder(idx));
    QCOMPARE(model.rowCount(), 1);
}

void tst_FolderModel::testRemoveFolder_nonRoot()
{
    FolderModel model;
    model.addFolder(m_testDir1);

    // Expand dir1 to get subfolders
    QModelIndex rootIdx = model.index(0, 0);
    QVERIFY(fetchMoreAndWait(model, rootIdx));
    QVERIFY(model.rowCount(rootIdx) > 0);

    // Try to remove a sub-folder — should fail
    QModelIndex childIdx = model.index(0, 0, rootIdx);
    QVERIFY(!model.removeFolder(childIdx));
}

void tst_FolderModel::testClearAll()
{
    FolderModel model;
    model.addFolder(m_testDir1);
    model.addFolder(m_testDir2);
    model.addFolder(m_testDir3);

    model.clearAll();
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(model.rootFolderPaths().isEmpty());
}

void tst_FolderModel::testRootFolderPaths()
{
    FolderModel model;
    model.addFolder(m_testDir1);
    model.addFolder(m_testDir2);

    QStringList paths = model.rootFolderPaths();
    QCOMPARE(paths.size(), 2);
    QVERIFY(paths.contains(m_testDir1));
    QVERIFY(paths.contains(m_testDir2));
}

void tst_FolderModel::testSetRootFolders()
{
    FolderModel model;
    model.setRootFolders({m_testDir1, m_testDir2, m_testDir3});
    QCOMPARE(model.rowCount(), 3);

    QStringList paths = model.rootFolderPaths();
    QVERIFY(paths.contains(m_testDir1));
    QVERIFY(paths.contains(m_testDir2));
    QVERIFY(paths.contains(m_testDir3));
}

void tst_FolderModel::testSetRootFolders_filterInvalid()
{
    FolderModel model;
    model.setRootFolders({m_testDir1, "/nonexistent/path", m_testDir2});
    QCOMPARE(model.rowCount(), 2); // Invalid path should be filtered out
}

void tst_FolderModel::testFilePath()
{
    FolderModel model;
    model.addFolder(m_testDir1);

    QModelIndex idx = model.index(0, 0);
    QCOMPARE(model.filePath(idx), m_testDir1);
}

void tst_FolderModel::testIsRootFolder()
{
    FolderModel model;
    model.addFolder(m_testDir1);

    QModelIndex rootIdx = model.index(0, 0);
    QVERIFY(model.isRootFolder(rootIdx));

    // Expand and check child
    QVERIFY(fetchMoreAndWait(model, rootIdx));
    if (model.rowCount(rootIdx) > 0) {
        QModelIndex childIdx = model.index(0, 0, rootIdx);
        QVERIFY(!model.isRootFolder(childIdx));
    }
}

void tst_FolderModel::testRefreshFolder()
{
    FolderModel model;
    model.addFolder(m_testDir1);

    QModelIndex rootIdx = model.index(0, 0);
    QVERIFY(fetchMoreAndWait(model, rootIdx));
    int childCount = model.rowCount(rootIdx);
    QVERIFY(childCount > 0);

    // Refresh should clear and allow re-fetch
    model.refreshFolder(rootIdx);
    QCOMPARE(model.rowCount(rootIdx), 0); // Children cleared
    QVERIFY(model.canFetchMore(rootIdx)); // Can fetch again
}

void tst_FolderModel::testRefreshAll()
{
    FolderModel model;
    model.addFolder(m_testDir1);
    model.addFolder(m_testDir2);

    // Fetch children for both
    for (int i = 0; i < model.rowCount(); ++i) {
        QModelIndex idx = model.index(i, 0);
        QVERIFY(fetchMoreAndWait(model, idx));
    }

    model.refreshAll();

    // All root nodes should be refreshable again
    for (int i = 0; i < model.rowCount(); ++i) {
        QModelIndex idx = model.index(i, 0);
        QVERIFY(model.canFetchMore(idx));
    }
}

void tst_FolderModel::testDataDisplayRole()
{
    FolderModel model;
    model.addFolder(m_testDir1);

    QModelIndex idx = model.index(0, 0);
    QString displayName = model.data(idx, Qt::DisplayRole).toString();
    QCOMPARE(displayName, "dir1");
}

void tst_FolderModel::testDataToolTipRole()
{
    FolderModel model;
    model.addFolder(m_testDir1);

    QModelIndex idx = model.index(0, 0);
    QString tooltip = model.data(idx, Qt::ToolTipRole).toString();
    QCOMPARE(tooltip, m_testDir1);
}

void tst_FolderModel::testDataPathRole()
{
    FolderModel model;
    model.addFolder(m_testDir1);

    QModelIndex idx = model.index(0, 0);
    QString path = model.data(idx, FolderModel::PathRole).toString();
    QCOMPARE(path, m_testDir1);
}

void tst_FolderModel::testDataIsRootRole()
{
    FolderModel model;
    model.addFolder(m_testDir1);

    QModelIndex rootIdx = model.index(0, 0);
    QVERIFY(model.data(rootIdx, FolderModel::IsRootRole).toBool());

    QVERIFY(fetchMoreAndWait(model, rootIdx));
    if (model.rowCount(rootIdx) > 0) {
        QModelIndex childIdx = model.index(0, 0, rootIdx);
        QVERIFY(!model.data(childIdx, FolderModel::IsRootRole).toBool());
    }
}

void tst_FolderModel::testColumnCount()
{
    FolderModel model;
    QCOMPARE(model.columnCount(), 1);

    model.addFolder(m_testDir1);
    QModelIndex idx = model.index(0, 0);
    QCOMPARE(model.columnCount(idx), 1);
}

void tst_FolderModel::testHasChildren()
{
    FolderModel model;
    model.addFolder(m_testDir1); // Has subdirs
    model.addFolder(m_testDir3); // Empty dir

    QModelIndex dir1Idx = model.index(0, 0);
    // Optimistic strategy: unfetched nodes always report hasChildren = true
    QVERIFY(model.hasChildren(dir1Idx));

    QModelIndex dir3Idx = model.index(1, 0);
    // Even empty dirs report true before fetching (optimistic)
    QVERIFY(model.hasChildren(dir3Idx));

    // After fetching dir3 (empty), hasChildren should return false
    QVERIFY(fetchMoreAndWait(model, dir3Idx));
    QVERIFY(!model.hasChildren(dir3Idx));

    // dir1 should still have children after fetch
    QVERIFY(fetchMoreAndWait(model, dir1Idx));
    QVERIFY(model.hasChildren(dir1Idx));
}

void tst_FolderModel::testFetchMore()
{
    FolderModel model;
    model.addFolder(m_testDir1);

    QModelIndex rootIdx = model.index(0, 0);
    QVERIFY(model.canFetchMore(rootIdx));
    QCOMPARE(model.rowCount(rootIdx), 0); // Not fetched yet

    QVERIFY(fetchMoreAndWait(model, rootIdx));
    QVERIFY(!model.canFetchMore(rootIdx)); // Now fetched
    QCOMPARE(model.rowCount(rootIdx), 2); // subA and subB
}

void tst_FolderModel::testFetchMoreAsync()
{
    // Verify that fetchStarted and fetchFinished signals are emitted
    FolderModel model;
    model.addFolder(m_testDir1);

    QModelIndex rootIdx = model.index(0, 0);

    QSignalSpy startSpy(&model, &FolderModel::fetchStarted);
    QSignalSpy finishSpy(&model, &FolderModel::fetchFinished);

    model.fetchMore(rootIdx);

    // fetchStarted should be emitted synchronously
    QCOMPARE(startSpy.count(), 1);

    // Wait for async completion
    QVERIFY(finishSpy.wait(5000));
    QCOMPARE(finishSpy.count(), 1);

    // Verify data is populated
    QCOMPARE(model.rowCount(rootIdx), 2);
}

void tst_FolderModel::testFetchMoreNoDuplicate()
{
    // Verify that calling fetchMore twice doesn't trigger duplicate fetches
    FolderModel model;
    model.addFolder(m_testDir1);

    QModelIndex rootIdx = model.index(0, 0);

    QSignalSpy finishSpy(&model, &FolderModel::fetchFinished);

    model.fetchMore(rootIdx);
    // Second call should be ignored (fetching flag is set)
    QVERIFY(!model.canFetchMore(rootIdx));
    model.fetchMore(rootIdx);

    QVERIFY(finishSpy.wait(5000));
    // Only one fetchFinished signal
    QCOMPARE(finishSpy.count(), 1);
    QCOMPARE(model.rowCount(rootIdx), 2);
}

void tst_FolderModel::testIndex()
{
    FolderModel model;
    model.addFolder(m_testDir1);
    model.addFolder(m_testDir2);

    QModelIndex idx0 = model.index(0, 0);
    QModelIndex idx1 = model.index(1, 0);
    QVERIFY(idx0.isValid());
    QVERIFY(idx1.isValid());

    // Invalid column
    QModelIndex invalidCol = model.index(0, 1);
    QVERIFY(!invalidCol.isValid());

    // Out of range row
    QModelIndex invalidRow = model.index(5, 0);
    QVERIFY(!invalidRow.isValid());
}

void tst_FolderModel::testParent()
{
    FolderModel model;
    model.addFolder(m_testDir1);

    // Root items have no parent
    QModelIndex rootIdx = model.index(0, 0);
    QVERIFY(!model.parent(rootIdx).isValid());

    // Child items' parent is the root
    QVERIFY(fetchMoreAndWait(model, rootIdx));
    if (model.rowCount(rootIdx) > 0) {
        QModelIndex childIdx = model.index(0, 0, rootIdx);
        QModelIndex parentIdx = model.parent(childIdx);
        QVERIFY(parentIdx.isValid());
        QCOMPARE(model.filePath(parentIdx), m_testDir1);
    }
}

QTEST_MAIN(tst_FolderModel)
#include "tst_FolderModel.moc"
