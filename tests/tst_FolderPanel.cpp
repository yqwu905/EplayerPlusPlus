#include <QTest>
#include <QLineEdit>
#include <QListWidget>
#include <QSettings>
#include <QTemporaryDir>

#include "models/FolderModel.h"
#include "services/SettingsManager.h"
#include "widgets/FolderPanel.h"

class tst_FolderPanel : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void enterPathAndPressReturn_addsFolder();
    void enterInvalidPathAndPressReturn_doesNotAddFolder();
    void rootFolderSummaryList_isNotShown();
    void pathInput_scalesHeightWithLargerFont();
};

void tst_FolderPanel::init()
{
    QCoreApplication::setOrganizationName("ImageCompareTests");
    QCoreApplication::setApplicationName("tst_FolderPanel");
    QSettings().clear();
}

void tst_FolderPanel::cleanup()
{
    QSettings().clear();
}

void tst_FolderPanel::enterPathAndPressReturn_addsFolder()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    SettingsManager settingsManager;
    FolderPanel panel(&settingsManager);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    auto *pathInput = panel.findChild<QLineEdit *>("folderPathInput");
    QVERIFY(pathInput != nullptr);

    pathInput->setFocus();
    QTest::keyClicks(pathInput, tempDir.path());
    QTest::keyClick(pathInput, Qt::Key_Return);

    const QStringList roots = panel.folderModel()->rootFolderPaths();
    QCOMPARE(roots.size(), 1);
    QCOMPARE(roots.first(), tempDir.path());
}

void tst_FolderPanel::enterInvalidPathAndPressReturn_doesNotAddFolder()
{
    SettingsManager settingsManager;
    FolderPanel panel(&settingsManager);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    auto *pathInput = panel.findChild<QLineEdit *>("folderPathInput");
    QVERIFY(pathInput != nullptr);

    pathInput->setFocus();
    QTest::keyClicks(pathInput, "/path/that/does/not/exist");
    QTest::keyClick(pathInput, Qt::Key_Return);

    QCOMPARE(panel.folderModel()->rootFolderPaths().size(), 0);
}

void tst_FolderPanel::rootFolderSummaryList_isNotShown()
{
    SettingsManager settingsManager;
    FolderPanel panel(&settingsManager);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    QVERIFY(panel.findChild<QListWidget *>("rootFolderList") == nullptr);
}

void tst_FolderPanel::pathInput_scalesHeightWithLargerFont()
{
    SettingsManager settingsManager;
    FolderPanel panel(&settingsManager);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    auto *pathInput = panel.findChild<QLineEdit *>("folderPathInput");
    QVERIFY(pathInput != nullptr);

    QFont largeFont = pathInput->font();
    largeFont.setPointSize(18);
    pathInput->setFont(largeFont);
    QCoreApplication::processEvents();

    QVERIFY2(pathInput->sizeHint().height() > 30,
             qPrintable(QStringLiteral("expected height > 30 for 18pt font, got %1")
                            .arg(pathInput->sizeHint().height())));
    QVERIFY(pathInput->height() >= pathInput->fontMetrics().height());
}

QTEST_MAIN(tst_FolderPanel)
#include "tst_FolderPanel.moc"
