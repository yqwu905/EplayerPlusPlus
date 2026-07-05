#include <QTest>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextEdit>
#include <QTemporaryDir>

#include "services/ImageMarkManager.h"
#include "services/SettingsManager.h"
#include "services/VlmAnnotationService.h"
#include "widgets/VlmAnnotationDialog.h"

class tst_VlmAnnotationDialog : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void columnsAndValidation();
    void loadsPersistedSettings();
    void resultReasonUsesTooltipForLongText();

private:
    static QList<VlmAnnotationService::ColumnSnapshot> sampleColumns();
};

void tst_VlmAnnotationDialog::init()
{
    QCoreApplication::setOrganizationName(QStringLiteral("ImageCompare_Test"));
    QCoreApplication::setApplicationName(QStringLiteral("tst_VlmAnnotationDialog"));
    QSettings().clear();
}

QList<VlmAnnotationService::ColumnSnapshot> tst_VlmAnnotationDialog::sampleColumns()
{
    QList<VlmAnnotationService::ColumnSnapshot> columns;
    for (int c = 0; c < 2; ++c) {
        VlmAnnotationService::ColumnSnapshot column;
        column.columnIndex = c;
        column.folderPath = QStringLiteral("/tmp/col%1").arg(c);
        column.columnName = QStringLiteral("col%1").arg(c);

        VlmAnnotationService::ImageItem image;
        image.columnIndex = c;
        image.row = 0;
        image.folderPath = column.folderPath;
        image.columnName = column.columnName;
        image.imagePath = QStringLiteral("/tmp/col%1/image.png").arg(c);
        image.fileName = QStringLiteral("image.png");
        column.images.append(image);
        columns.append(column);
    }
    return columns;
}

void tst_VlmAnnotationDialog::columnsAndValidation()
{
    SettingsManager settings;
    SettingsManager::VlmProvider provider;
    provider.id = QStringLiteral("provider-a");
    provider.name = QStringLiteral("Provider A");
    provider.baseUrl = QStringLiteral("https://example.test/v1");
    provider.model = QStringLiteral("vision-model");
    provider.apiKey = QStringLiteral("secret");
    settings.setVlmProviders({provider});

    ImageMarkManager marks;
    VlmAnnotationDialog dialog(sampleColumns(), &settings, &marks);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));

    auto *providerCombo = dialog.findChild<QComboBox *>(QStringLiteral("vlmProviderCombo"));
    auto *manageProviders = dialog.findChild<QPushButton *>(QStringLiteral("vlmManageProvidersButton"));
    auto *prompt = dialog.findChild<QTextEdit *>(QStringLiteral("vlmPromptEdit"));
    auto *target = dialog.findChild<QComboBox *>(QStringLiteral("vlmTargetCombo"));
    auto *references = dialog.findChild<QListWidget *>(QStringLiteral("vlmReferenceList"));
    auto *maxItems = dialog.findChild<QSpinBox *>(QStringLiteral("vlmMaxItemsSpin"));
    auto *concurrency = dialog.findChild<QSpinBox *>(QStringLiteral("vlmConcurrencySpin"));
    auto *skipMarked = dialog.findChild<QCheckBox *>(QStringLiteral("vlmSkipMarkedCheck"));
    auto *run = dialog.findChild<QPushButton *>(QStringLiteral("vlmRunButton"));
    QVERIFY(providerCombo != nullptr);
    QVERIFY(manageProviders != nullptr);
    QVERIFY(prompt != nullptr);
    QVERIFY(target != nullptr);
    QVERIFY(references != nullptr);
    QVERIFY(maxItems != nullptr);
    QVERIFY(concurrency != nullptr);
    QVERIFY(skipMarked != nullptr);
    QVERIFY(run != nullptr);

    QCOMPARE(providerCombo->count(), 1);
    QCOMPARE(providerCombo->currentData().toString(), QStringLiteral("provider-a"));
    QCOMPARE(target->count(), 2);
    QCOMPARE(references->count(), 2);
    QCOMPARE(maxItems->value(), 0);
    QCOMPARE(concurrency->value(), 1);
    QCOMPARE(concurrency->minimum(), 1);
    QCOMPARE(skipMarked->isChecked(), true);
    QVERIFY(!run->isEnabled());

    QVERIFY(!(references->item(0)->flags() & Qt::ItemIsEnabled));
    QCOMPARE(references->item(0)->checkState(), Qt::Unchecked);
    QVERIFY(references->item(1)->flags() & Qt::ItemIsEnabled);
    QCOMPARE(references->item(1)->checkState(), Qt::Checked);

    prompt->setPlainText(QStringLiteral("A is best, F is worst."));
    QTRY_VERIFY(run->isEnabled());
}

void tst_VlmAnnotationDialog::loadsPersistedSettings()
{
    SettingsManager settings;
    SettingsManager::VlmProvider providerA;
    providerA.id = QStringLiteral("a");
    providerA.name = QStringLiteral("Provider A");
    providerA.baseUrl = QStringLiteral("https://a.test/v1");
    providerA.model = QStringLiteral("model-a");
    providerA.apiKey = QStringLiteral("key-a");
    SettingsManager::VlmProvider providerB;
    providerB.id = QStringLiteral("b");
    providerB.name = QStringLiteral("Provider B");
    providerB.baseUrl = QStringLiteral("https://b.test/v1");
    providerB.model = QStringLiteral("model-b");
    providerB.apiKey = QStringLiteral("key-b");
    settings.setVlmProviders({providerA, providerB});
    settings.setActiveVlmProviderId(QStringLiteral("b"));
    settings.setVlmUserPrompt(QStringLiteral("saved prompt"));
    settings.setVlmMatchRule(2);
    settings.setVlmMaxItems(12);
    settings.setVlmConcurrency(4);
    settings.setVlmSkipMarked(false);

    ImageMarkManager marks;
    VlmAnnotationDialog dialog(sampleColumns(), &settings, &marks);

    QCOMPARE(dialog.findChild<QComboBox *>(QStringLiteral("vlmProviderCombo"))->currentData().toString(),
             QStringLiteral("b"));
    QCOMPARE(dialog.findChild<QTextEdit *>(QStringLiteral("vlmPromptEdit"))->toPlainText(),
             QStringLiteral("saved prompt"));
    QCOMPARE(dialog.findChild<QComboBox *>(QStringLiteral("vlmMatchRuleCombo"))->currentData().toInt(),
             2);
    QCOMPARE(dialog.findChild<QSpinBox *>(QStringLiteral("vlmMaxItemsSpin"))->value(), 12);
    QCOMPARE(dialog.findChild<QSpinBox *>(QStringLiteral("vlmConcurrencySpin"))->value(), 4);
    QCOMPARE(dialog.findChild<QCheckBox *>(QStringLiteral("vlmSkipMarkedCheck"))->isChecked(), false);
}

void tst_VlmAnnotationDialog::resultReasonUsesTooltipForLongText()
{
    SettingsManager settings;
    SettingsManager::VlmProvider provider;
    provider.id = QStringLiteral("provider-a");
    provider.name = QStringLiteral("Provider A");
    provider.baseUrl = QStringLiteral("https://example.test/v1");
    provider.model = QStringLiteral("vision-model");
    settings.setVlmProviders({provider});

    ImageMarkManager marks;
    VlmAnnotationDialog dialog(sampleColumns(), &settings, &marks);
    auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("vlmResultTable"));
    QVERIFY(table != nullptr);
    QVERIFY(table->wordWrap());
    QCOMPARE(table->textElideMode(), Qt::ElideNone);
}

QTEST_MAIN(tst_VlmAnnotationDialog)
#include "tst_VlmAnnotationDialog.moc"
