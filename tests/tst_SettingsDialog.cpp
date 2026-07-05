#include <QTest>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>

#include "services/SettingsManager.h"
#include "widgets/SettingsDialog.h"

class tst_SettingsDialog : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void containsVlmSettingsTab();
    void savePersistsVlmProviders();
};

void tst_SettingsDialog::init()
{
    QCoreApplication::setOrganizationName(QStringLiteral("ImageCompare_Test"));
    QCoreApplication::setApplicationName(QStringLiteral("tst_SettingsDialog"));
    QSettings().clear();
}

void tst_SettingsDialog::containsVlmSettingsTab()
{
    SettingsManager settings;
    SettingsDialog dialog(&settings);

    auto *tabs = dialog.findChild<QTabWidget *>(QStringLiteral("settingsTabWidget"));
    auto *baseUrl = dialog.findChild<QLineEdit *>(QStringLiteral("vlmProviderBaseUrlEdit"));
    auto *model = dialog.findChild<QLineEdit *>(QStringLiteral("vlmProviderModelEdit"));
    auto *test = dialog.findChild<QPushButton *>(QStringLiteral("vlmProviderTestButton"));
    auto *save = dialog.findChild<QPushButton *>(QStringLiteral("settingsSaveButton"));
    QVERIFY(tabs != nullptr);
    QVERIFY(baseUrl != nullptr);
    QVERIFY(model != nullptr);
    QVERIFY(test != nullptr);
    QVERIFY(save != nullptr);

    QCOMPARE(tabs->count(), 1);
    QCOMPARE(tabs->tabText(0), QStringLiteral("VLM"));
    QCOMPARE(baseUrl->text(), QStringLiteral("https://api.openai.com/v1"));
    QVERIFY(!save->isEnabled());

    model->setText(QStringLiteral("vision-model"));
    QTRY_VERIFY(save->isEnabled());
}

void tst_SettingsDialog::savePersistsVlmProviders()
{
    SettingsManager settings;
    SettingsDialog dialog(&settings);

    auto *name = dialog.findChild<QLineEdit *>(QStringLiteral("vlmProviderNameEdit"));
    auto *baseUrl = dialog.findChild<QLineEdit *>(QStringLiteral("vlmProviderBaseUrlEdit"));
    auto *model = dialog.findChild<QLineEdit *>(QStringLiteral("vlmProviderModelEdit"));
    auto *apiKey = dialog.findChild<QLineEdit *>(QStringLiteral("vlmProviderApiKeyEdit"));
    auto *save = dialog.findChild<QPushButton *>(QStringLiteral("settingsSaveButton"));
    QVERIFY(name != nullptr);
    QVERIFY(baseUrl != nullptr);
    QVERIFY(model != nullptr);
    QVERIFY(apiKey != nullptr);
    QVERIFY(save != nullptr);

    name->setText(QStringLiteral("Provider A"));
    baseUrl->setText(QStringLiteral("https://a.test/v1"));
    model->setText(QStringLiteral("model-a"));
    apiKey->setText(QStringLiteral("key-a"));
    QTRY_VERIFY(save->isEnabled());
    QTest::mouseClick(save, Qt::LeftButton);

    const QList<SettingsManager::VlmProvider> providers = settings.vlmProviders();
    QCOMPARE(providers.size(), 1);
    QCOMPARE(providers.first().name, QStringLiteral("Provider A"));
    QCOMPARE(providers.first().baseUrl, QStringLiteral("https://a.test/v1"));
    QCOMPARE(providers.first().model, QStringLiteral("model-a"));
    QCOMPARE(providers.first().apiKey, QStringLiteral("key-a"));
}

QTEST_MAIN(tst_SettingsDialog)
#include "tst_SettingsDialog.moc"
