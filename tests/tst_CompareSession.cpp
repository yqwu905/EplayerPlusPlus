#include <QtTest>
#include <QSignalSpy>

#include "models/CompareSession.h"

class tst_CompareSession : public QObject
{
    Q_OBJECT

private slots:
    void testInitialState();
    void testAddFolder();
    void testAddFolder_duplicate();
    void testAddFolder_maxReached();
    void testRemoveFolder();
    void testRemoveFolder_notFound();
    void testRemoveFolderAt();
    void testRemoveFolderAt_invalid();
    void testClear();
    void testClear_empty();
    void testContainsFolder();
    void testIsFull();
    void testIndexOf();
    void testFolderCount();

    void testSignal_folderAdded();
    void testSignal_folderRemoved();
    void testSignal_cleared();
    void testSignal_sessionChanged();
};

void tst_CompareSession::testInitialState()
{
    CompareSession session;
    QCOMPARE(session.folderCount(), 0);
    QVERIFY(session.folders().isEmpty());
    QVERIFY(!session.isFull());
}

void tst_CompareSession::testAddFolder()
{
    CompareSession session;
    QVERIFY(session.addFolder("/path/a"));
    QVERIFY(session.addFolder("/path/b"));
    QCOMPARE(session.folderCount(), 2);
    QCOMPARE(session.folders(), QStringList({"/path/a", "/path/b"}));
}

void tst_CompareSession::testAddFolder_duplicate()
{
    CompareSession session;
    QVERIFY(session.addFolder("/path/a"));
    QVERIFY(!session.addFolder("/path/a"));
    QCOMPARE(session.folderCount(), 1);
}

void tst_CompareSession::testAddFolder_maxReached()
{
    CompareSession session;
    for (int i = 0; i < CompareSession::MaxFolders; ++i) {
        QVERIFY(session.addFolder(QString("/path/%1").arg(i)));
    }
    QVERIFY(session.isFull());
    QVERIFY(!session.addFolder("/path/extra"));
    QCOMPARE(session.folderCount(), CompareSession::MaxFolders);
}

void tst_CompareSession::testRemoveFolder()
{
    CompareSession session;
    session.addFolder("/path/a");
    session.addFolder("/path/b");

    QVERIFY(session.removeFolder("/path/a"));
    QCOMPARE(session.folderCount(), 1);
    QVERIFY(!session.containsFolder("/path/a"));
    QVERIFY(session.containsFolder("/path/b"));
}

void tst_CompareSession::testRemoveFolder_notFound()
{
    CompareSession session;
    session.addFolder("/path/a");
    QVERIFY(!session.removeFolder("/path/nonexistent"));
    QCOMPARE(session.folderCount(), 1);
}

void tst_CompareSession::testRemoveFolderAt()
{
    CompareSession session;
    session.addFolder("/path/a");
    session.addFolder("/path/b");
    session.addFolder("/path/c");

    QVERIFY(session.removeFolderAt(1)); // Remove "/path/b"
    QCOMPARE(session.folderCount(), 2);
    QCOMPARE(session.folders(), QStringList({"/path/a", "/path/c"}));
}

void tst_CompareSession::testRemoveFolderAt_invalid()
{
    CompareSession session;
    session.addFolder("/path/a");

    QVERIFY(!session.removeFolderAt(-1));
    QVERIFY(!session.removeFolderAt(5));
    QCOMPARE(session.folderCount(), 1);
}

void tst_CompareSession::testClear()
{
    CompareSession session;
    session.addFolder("/path/a");
    session.addFolder("/path/b");

    session.clear();
    QCOMPARE(session.folderCount(), 0);
    QVERIFY(session.folders().isEmpty());
}

void tst_CompareSession::testClear_empty()
{
    CompareSession session;
    QSignalSpy spy(&session, &CompareSession::cleared);
    session.clear(); // Should be a no-op
    QCOMPARE(spy.count(), 0);
}

void tst_CompareSession::testContainsFolder()
{
    CompareSession session;
    session.addFolder("/path/a");

    QVERIFY(session.containsFolder("/path/a"));
    QVERIFY(!session.containsFolder("/path/b"));
}

void tst_CompareSession::testIsFull()
{
    CompareSession session;
    QVERIFY(!session.isFull());

    for (int i = 0; i < CompareSession::MaxFolders; ++i) {
        session.addFolder(QString("/path/%1").arg(i));
    }
    QVERIFY(session.isFull());

    session.removeFolderAt(0);
    QVERIFY(!session.isFull());
}

void tst_CompareSession::testIndexOf()
{
    CompareSession session;
    session.addFolder("/path/a");
    session.addFolder("/path/b");
    session.addFolder("/path/c");

    QCOMPARE(session.indexOf("/path/a"), 0);
    QCOMPARE(session.indexOf("/path/b"), 1);
    QCOMPARE(session.indexOf("/path/c"), 2);
    QCOMPARE(session.indexOf("/path/d"), -1);
}

void tst_CompareSession::testFolderCount()
{
    CompareSession session;
    QCOMPARE(session.folderCount(), 0);

    session.addFolder("/path/a");
    QCOMPARE(session.folderCount(), 1);

    session.addFolder("/path/b");
    QCOMPARE(session.folderCount(), 2);

    session.removeFolder("/path/a");
    QCOMPARE(session.folderCount(), 1);
}

void tst_CompareSession::testSignal_folderAdded()
{
    CompareSession session;
    QSignalSpy spy(&session, &CompareSession::folderAdded);

    session.addFolder("/path/a");
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QString("/path/a"));
    QCOMPARE(spy.at(0).at(1).toInt(), 0);

    session.addFolder("/path/b");
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toString(), QString("/path/b"));
    QCOMPARE(spy.at(1).at(1).toInt(), 1);
}

void tst_CompareSession::testSignal_folderRemoved()
{
    CompareSession session;
    session.addFolder("/path/a");
    session.addFolder("/path/b");

    QSignalSpy spy(&session, &CompareSession::folderRemoved);

    session.removeFolder("/path/a");
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QString("/path/a"));
    QCOMPARE(spy.at(0).at(1).toInt(), 0);
}

void tst_CompareSession::testSignal_cleared()
{
    CompareSession session;
    session.addFolder("/path/a");

    QSignalSpy spy(&session, &CompareSession::cleared);
    session.clear();
    QCOMPARE(spy.count(), 1);
}

void tst_CompareSession::testSignal_sessionChanged()
{
    CompareSession session;
    QSignalSpy spy(&session, &CompareSession::sessionChanged);

    session.addFolder("/path/a");
    QCOMPARE(spy.count(), 1);

    session.addFolder("/path/b");
    QCOMPARE(spy.count(), 2);

    session.removeFolder("/path/a");
    QCOMPARE(spy.count(), 3);

    session.clear();
    QCOMPARE(spy.count(), 4);
}

QTEST_MAIN(tst_CompareSession)
#include "tst_CompareSession.moc"
