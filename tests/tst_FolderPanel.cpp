#include <QTest>
#include <QLineEdit>
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

QTEST_MAIN(tst_FolderPanel)
#include "tst_FolderPanel.moc"
