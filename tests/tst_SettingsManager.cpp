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

    void testIgnoreImageColorProfile_defaultOn();
    void testSetIgnoreImageColorProfile();
    void testVlmDefaults();
    void testVlmSettingsPersist();
    void testVlmApiKeyOnlyReadWhenRemembered();
    void testVlmProvidersPersistAndActive();
    void testVlmProvidersMigrateLegacyFields();

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

void tst_SettingsManager::testIgnoreImageColorProfile_defaultOn()
{
    // A pixel-diff tool wants determinism by default, so the setting starts
    // enabled on first launch.
    QCOMPARE(m_manager->ignoreImageColorProfile(), true);
}

void tst_SettingsManager::testSetIgnoreImageColorProfile()
{
    m_manager->setIgnoreImageColorProfile(false);
    QCOMPARE(m_manager->ignoreImageColorProfile(), false);

    m_manager->setIgnoreImageColorProfile(true);
    QCOMPARE(m_manager->ignoreImageColorProfile(), true);
}

void tst_SettingsManager::testVlmDefaults()
{
    QCOMPARE(m_manager->vlmBaseUrl(), QStringLiteral("https://api.openai.com/v1"));
    QVERIFY(m_manager->vlmModel().isEmpty());
    QVERIFY(m_manager->vlmUserPrompt().isEmpty());
    QCOMPARE(m_manager->vlmMatchRule(), 0);
    QCOMPARE(m_manager->vlmMaxItems(), 0);
    QCOMPARE(m_manager->vlmConcurrency(), 1);
    QCOMPARE(m_manager->vlmSkipMarked(), true);
    QCOMPARE(m_manager->vlmRememberApiKey(), false);
    QVERIFY(m_manager->vlmApiKey().isEmpty());
}

void tst_SettingsManager::testVlmSettingsPersist()
{
    m_manager->setVlmBaseUrl(QStringLiteral(" https://example.test/v1/ "));
    m_manager->setVlmModel(QStringLiteral(" model-x "));
    m_manager->setVlmUserPrompt(QStringLiteral("classify strictly"));
    m_manager->setVlmMatchRule(2);
    m_manager->setVlmMaxItems(37);
    m_manager->setVlmConcurrency(3);
    m_manager->setVlmSkipMarked(false);

    QCOMPARE(m_manager->vlmBaseUrl(), QStringLiteral("https://example.test/v1/"));
    QCOMPARE(m_manager->vlmModel(), QStringLiteral("model-x"));
    QCOMPARE(m_manager->vlmUserPrompt(), QStringLiteral("classify strictly"));
    QCOMPARE(m_manager->vlmMatchRule(), 2);
    QCOMPARE(m_manager->vlmMaxItems(), 37);
    QCOMPARE(m_manager->vlmConcurrency(), 3);
    QCOMPARE(m_manager->vlmSkipMarked(), false);

    m_manager->setVlmMatchRule(99);
    QCOMPARE(m_manager->vlmMatchRule(), 2);
    m_manager->setVlmMaxItems(-5);
    QCOMPARE(m_manager->vlmMaxItems(), 0);
    m_manager->setVlmConcurrency(0);
    QCOMPARE(m_manager->vlmConcurrency(), 1);
    m_manager->setVlmConcurrency(99);
    QCOMPARE(m_manager->vlmConcurrency(), 16);
}

void tst_SettingsManager::testVlmApiKeyOnlyReadWhenRemembered()
{
    m_manager->setVlmApiKey(QStringLiteral("secret"));
    QVERIFY(m_manager->vlmApiKey().isEmpty());

    m_manager->setVlmRememberApiKey(true);
    m_manager->setVlmApiKey(QStringLiteral("secret"));
    QCOMPARE(m_manager->vlmApiKey(), QStringLiteral("secret"));

    m_manager->setVlmRememberApiKey(false);
    QVERIFY(m_manager->vlmApiKey().isEmpty());
}

void tst_SettingsManager::testVlmProvidersPersistAndActive()
{
    SettingsManager::VlmProvider a;
    a.id = QStringLiteral("a");
    a.name = QStringLiteral("OpenAI");
    a.baseUrl = QStringLiteral("https://api.openai.com/v1");
    a.model = QStringLiteral("gpt-4.1");
    a.apiKey = QStringLiteral("key-a");

    SettingsManager::VlmProvider b;
    b.id = QStringLiteral("b");
    b.name = QStringLiteral("Local");
    b.baseUrl = QStringLiteral("http://127.0.0.1:8000/v1");
    b.model = QStringLiteral("qwen-vl");
    b.apiKey = QStringLiteral("key-b");

    m_manager->setVlmProviders({a, b});
    m_manager->setActiveVlmProviderId(QStringLiteral("b"));

    const QList<SettingsManager::VlmProvider> providers = m_manager->vlmProviders();
    QCOMPARE(providers.size(), 2);
    QCOMPARE(providers.at(0).name, QStringLiteral("OpenAI"));
    QCOMPARE(providers.at(1).apiKey, QStringLiteral("key-b"));
    QCOMPARE(m_manager->activeVlmProviderId(), QStringLiteral("b"));
    QCOMPARE(m_manager->activeVlmProvider().model, QStringLiteral("qwen-vl"));

    m_manager->setVlmProviders({a});
    QCOMPARE(m_manager->activeVlmProviderId(), QStringLiteral("a"));
}

void tst_SettingsManager::testVlmProvidersMigrateLegacyFields()
{
    m_manager->setVlmBaseUrl(QStringLiteral("https://legacy.test/v1"));
    m_manager->setVlmModel(QStringLiteral("legacy-model"));
    m_manager->setVlmRememberApiKey(true);
    m_manager->setVlmApiKey(QStringLiteral("legacy-key"));

    const QList<SettingsManager::VlmProvider> providers = m_manager->vlmProviders();
    QCOMPARE(providers.size(), 1);
    QCOMPARE(providers.first().name, QStringLiteral("legacy-model"));
    QCOMPARE(providers.first().baseUrl, QStringLiteral("https://legacy.test/v1"));
    QCOMPARE(providers.first().model, QStringLiteral("legacy-model"));
    QCOMPARE(providers.first().apiKey, QStringLiteral("legacy-key"));
}

QTEST_MAIN(tst_SettingsManager)
#include "tst_SettingsManager.moc"
