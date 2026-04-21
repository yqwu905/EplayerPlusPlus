#include <QtTest>
#include <QCoreApplication>

#include "services/SettingsManager.h"

class tst_SettingsManager : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void testSaveFolderList();
    void testLoadFolderList_empty();
    void testAddFolder();
    void testAddFolder_duplicate();
    void testRemoveFolder();
    void testRemoveFolder_nonExistent();
    void testClearFolderList();

    void testComparisonThreshold_default();
    void testSetComparisonThreshold();
    void testSetComparisonThreshold_clamped();
    void testResizeToFirstImage_defaultOff();
    void testResizeToFirstImage_setAndRead();

private:
    SettingsManager *m_manager = nullptr;
};

void tst_SettingsManager::init()
{
    // Use a unique organization/app name to avoid polluting real settings
    QCoreApplication::setOrganizationName("ImageCompare_Test");
    QCoreApplication::setApplicationName("tst_SettingsManager");

    // Clear ALL leftover settings from previous test runs
    QSettings settings;
    settings.clear();

    m_manager = new SettingsManager(this);
}

void tst_SettingsManager::testSaveFolderList()
{
    QStringList folders = {"/path/to/folder1", "/path/to/folder2", "/path/to/folder3"};
    m_manager->saveFolderList(folders);

    QStringList loaded = m_manager->loadFolderList();
    QCOMPARE(loaded, folders);
}

void tst_SettingsManager::testLoadFolderList_empty()
{
    QStringList loaded = m_manager->loadFolderList();
    QVERIFY(loaded.isEmpty());
}

void tst_SettingsManager::testAddFolder()
{
    m_manager->addFolder("/path/to/folderA");
    m_manager->addFolder("/path/to/folderB");

    QStringList loaded = m_manager->loadFolderList();
    QCOMPARE(loaded.size(), 2);
    QVERIFY(loaded.contains("/path/to/folderA"));
    QVERIFY(loaded.contains("/path/to/folderB"));
}

void tst_SettingsManager::testAddFolder_duplicate()
{
    m_manager->addFolder("/path/to/folder");
    m_manager->addFolder("/path/to/folder"); // duplicate

    QStringList loaded = m_manager->loadFolderList();
    QCOMPARE(loaded.size(), 1); // Should not duplicate
}

void tst_SettingsManager::testRemoveFolder()
{
    m_manager->addFolder("/path/to/folderA");
    m_manager->addFolder("/path/to/folderB");
    m_manager->removeFolder("/path/to/folderA");

    QStringList loaded = m_manager->loadFolderList();
    QCOMPARE(loaded.size(), 1);
    QVERIFY(loaded.contains("/path/to/folderB"));
}

void tst_SettingsManager::testRemoveFolder_nonExistent()
{
    m_manager->addFolder("/path/to/folder");
    m_manager->removeFolder("/path/to/nonexistent");

    QStringList loaded = m_manager->loadFolderList();
    QCOMPARE(loaded.size(), 1);
}

void tst_SettingsManager::testClearFolderList()
{
    m_manager->addFolder("/path/to/folderA");
    m_manager->addFolder("/path/to/folderB");
    m_manager->clearFolderList();

    QStringList loaded = m_manager->loadFolderList();
    QVERIFY(loaded.isEmpty());
}

void tst_SettingsManager::testComparisonThreshold_default()
{
    QCOMPARE(m_manager->comparisonThreshold(), 10);
}

void tst_SettingsManager::testSetComparisonThreshold()
{
    m_manager->setComparisonThreshold(50);
    QCOMPARE(m_manager->comparisonThreshold(), 50);
}

void tst_SettingsManager::testSetComparisonThreshold_clamped()
{
    m_manager->setComparisonThreshold(-10);
    QCOMPARE(m_manager->comparisonThreshold(), 0);

    m_manager->setComparisonThreshold(300);
    QCOMPARE(m_manager->comparisonThreshold(), 255);
}

void tst_SettingsManager::testResizeToFirstImage_defaultOff()
{
    QCOMPARE(m_manager->resizeToFirstImageEnabled(), false);
}

void tst_SettingsManager::testResizeToFirstImage_setAndRead()
{
    m_manager->setResizeToFirstImageEnabled(true);
    QCOMPARE(m_manager->resizeToFirstImageEnabled(), true);

    m_manager->setResizeToFirstImageEnabled(false);
    QCOMPARE(m_manager->resizeToFirstImageEnabled(), false);
}

QTEST_MAIN(tst_SettingsManager)
#include "tst_SettingsManager.moc"
