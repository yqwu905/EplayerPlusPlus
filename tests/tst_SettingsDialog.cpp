#include <QTest>
#include <QLineEdit>
#include <QCheckBox>
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
    void performanceTabPersistsWindowsNetworkOptions();
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

    QCOMPARE(tabs->count(), 2);
    QCOMPARE(tabs->tabText(0), QStringLiteral("VLM"));
    QCOMPARE(tabs->tabText(1), QStringLiteral("性能"));
    QCOMPARE(baseUrl->text(), QStringLiteral("https://api.openai.com/v1"));
    QVERIFY(!save->isEnabled());

    model->setText(QStringLiteral("vision-model"));
    QTRY_VERIFY(save->isEnabled());
}

void tst_SettingsDialog::performanceTabPersistsWindowsNetworkOptions()
{
    SettingsManager settings;
    SettingsDialog dialog(&settings);

    auto *tabs = dialog.findChild<QTabWidget *>(QStringLiteral("settingsTabWidget"));
    auto *master = dialog.findChild<QCheckBox *>(
        QStringLiteral("windowsNetworkOptimizationsCheckBox"));
    auto *fast = dialog.findChild<QCheckBox *>(
        QStringLiteral("windowsNetworkFastEnumerationCheckBox"));
    auto *prefetch = dialog.findChild<QCheckBox *>(
        QStringLiteral("windowsNetworkConstrainedPrefetchCheckBox"));
    auto *deferred = dialog.findChild<QCheckBox *>(
        QStringLiteral("windowsNetworkDeferredValidationCheckBox"));
    auto *save = dialog.findChild<QPushButton *>(QStringLiteral("settingsSaveButton"));
    QVERIFY(tabs != nullptr);
    QVERIFY(master != nullptr);
    QVERIFY(fast != nullptr);
    QVERIFY(prefetch != nullptr);
    QVERIFY(deferred != nullptr);
    QVERIFY(save != nullptr);

    tabs->setCurrentIndex(static_cast<int>(SettingsDialog::Tab::Performance));
    QVERIFY(save->isEnabled());
    QTest::mouseClick(master, Qt::LeftButton);
    QVERIFY(fast->isEnabled());
    QTest::mouseClick(fast, Qt::LeftButton);
    QVERIFY(prefetch->isChecked());
    QVERIFY(deferred->isChecked());
    QTest::mouseClick(save, Qt::LeftButton);

    QCOMPARE(settings.windowsNetworkOptimizationsEnabled(), true);
    QCOMPARE(settings.windowsNetworkFastEnumerationEnabled(), false);
    QCOMPARE(settings.windowsNetworkConstrainedPrefetchEnabled(), true);
    QCOMPARE(settings.windowsNetworkDeferredFolderValidationEnabled(), true);
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
