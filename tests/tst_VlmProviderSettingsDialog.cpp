#include <QTest>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>

#include "services/SettingsManager.h"
#include "widgets/VlmProviderSettingsDialog.h"

class tst_VlmProviderSettingsDialog : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void editsAndPersistsProviders();
    void startsWithDefaultProviderWhenEmpty();
    void testButtonRequiresValidProvider();
};

void tst_VlmProviderSettingsDialog::init()
{
    QCoreApplication::setOrganizationName(QStringLiteral("ImageCompare_Test"));
    QCoreApplication::setApplicationName(QStringLiteral("tst_VlmProviderSettingsDialog"));
    QSettings().clear();
}

void tst_VlmProviderSettingsDialog::editsAndPersistsProviders()
{
    SettingsManager settings;
    SettingsManager::VlmProvider provider;
    provider.id = QStringLiteral("a");
    provider.name = QStringLiteral("Old");
    provider.baseUrl = QStringLiteral("https://old.test/v1");
    provider.model = QStringLiteral("old-model");
    provider.apiKey = QStringLiteral("old-key");
    settings.setVlmProviders({provider});

    VlmProviderSettingsDialog dialog(&settings);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));

    auto *list = dialog.findChild<QListWidget *>(QStringLiteral("vlmProviderList"));
    auto *name = dialog.findChild<QLineEdit *>(QStringLiteral("vlmProviderNameEdit"));
    auto *baseUrl = dialog.findChild<QLineEdit *>(QStringLiteral("vlmProviderBaseUrlEdit"));
    auto *model = dialog.findChild<QLineEdit *>(QStringLiteral("vlmProviderModelEdit"));
    auto *apiKey = dialog.findChild<QLineEdit *>(QStringLiteral("vlmProviderApiKeyEdit"));
    auto *save = dialog.findChild<QPushButton *>(QStringLiteral("vlmProviderSaveButton"));
    auto *test = dialog.findChild<QPushButton *>(QStringLiteral("vlmProviderTestButton"));
    auto *testStatus = dialog.findChild<QLabel *>(QStringLiteral("vlmProviderTestStatusLabel"));
    QVERIFY(list != nullptr);
    QVERIFY(name != nullptr);
    QVERIFY(baseUrl != nullptr);
    QVERIFY(model != nullptr);
    QVERIFY(apiKey != nullptr);
    QVERIFY(save != nullptr);
    QVERIFY(test != nullptr);
    QVERIFY(testStatus != nullptr);

    QCOMPARE(list->count(), 1);
    QVERIFY(test->isEnabled());
    name->setText(QStringLiteral("New"));
    baseUrl->setText(QStringLiteral("https://new.test/v1"));
    model->setText(QStringLiteral("new-model"));
    apiKey->setText(QStringLiteral("new-key"));
    QVERIFY(save->isEnabled());
    QTest::mouseClick(save, Qt::LeftButton);

    const QList<SettingsManager::VlmProvider> providers = settings.vlmProviders();
    QCOMPARE(providers.size(), 1);
    QCOMPARE(providers.first().name, QStringLiteral("New"));
    QCOMPARE(providers.first().baseUrl, QStringLiteral("https://new.test/v1"));
    QCOMPARE(providers.first().model, QStringLiteral("new-model"));
    QCOMPARE(providers.first().apiKey, QStringLiteral("new-key"));
    QCOMPARE(settings.activeVlmProviderId(), providers.first().id);
}

void tst_VlmProviderSettingsDialog::startsWithDefaultProviderWhenEmpty()
{
    SettingsManager settings;
    VlmProviderSettingsDialog dialog(&settings);
    auto *list = dialog.findChild<QListWidget *>(QStringLiteral("vlmProviderList"));
    auto *baseUrl = dialog.findChild<QLineEdit *>(QStringLiteral("vlmProviderBaseUrlEdit"));
    auto *save = dialog.findChild<QPushButton *>(QStringLiteral("vlmProviderSaveButton"));
    auto *test = dialog.findChild<QPushButton *>(QStringLiteral("vlmProviderTestButton"));
    QVERIFY(list != nullptr);
    QVERIFY(baseUrl != nullptr);
    QVERIFY(save != nullptr);
    QVERIFY(test != nullptr);
    QCOMPARE(list->count(), 1);
    QCOMPARE(baseUrl->text(), QStringLiteral("https://api.openai.com/v1"));
    QVERIFY(!save->isEnabled());
    QVERIFY(!test->isEnabled());
}

void tst_VlmProviderSettingsDialog::testButtonRequiresValidProvider()
{
    SettingsManager settings;
    VlmProviderSettingsDialog dialog(&settings);
    auto *name = dialog.findChild<QLineEdit *>(QStringLiteral("vlmProviderNameEdit"));
    auto *baseUrl = dialog.findChild<QLineEdit *>(QStringLiteral("vlmProviderBaseUrlEdit"));
    auto *model = dialog.findChild<QLineEdit *>(QStringLiteral("vlmProviderModelEdit"));
    auto *test = dialog.findChild<QPushButton *>(QStringLiteral("vlmProviderTestButton"));
    QVERIFY(name != nullptr);
    QVERIFY(baseUrl != nullptr);
    QVERIFY(model != nullptr);
    QVERIFY(test != nullptr);

    QVERIFY(!test->isEnabled());
    name->setText(QStringLiteral("Local"));
    baseUrl->setText(QStringLiteral("http://127.0.0.1:11434/v1"));
    model->setText(QStringLiteral("vlm-model"));
    QVERIFY(test->isEnabled());
}

QTEST_MAIN(tst_VlmProviderSettingsDialog)
#include "tst_VlmProviderSettingsDialog.moc"
