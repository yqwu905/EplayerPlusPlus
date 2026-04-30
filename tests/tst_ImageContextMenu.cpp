#include <QAction>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QMenu>
#include <QTemporaryDir>
#include <QTest>

#include "widgets/ImageContextMenu.h"

class tst_ImageContextMenu : public QObject
{
    Q_OBJECT

private slots:
    void copyImage_copiesImageFromFile();
    void copyImage_prefersProvidedDisplayImage();
    void copyImageAddress_copiesAbsoluteNativePath();
    void menuActions_haveExpectedState();
    void openAndReveal_rejectMissingFiles();
};

void tst_ImageContextMenu::copyImage_copiesImageFromFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString imagePath = dir.filePath(QStringLiteral("source.png"));
    QImage image(7, 5, QImage::Format_ARGB32);
    image.fill(QColor(12, 34, 56));
    QVERIFY(image.save(imagePath));

    QVERIFY(ImageContextMenu::copyImage(imagePath));

    const QImage clipboardImage = QGuiApplication::clipboard()->image();
    QCOMPARE(clipboardImage.size(), image.size());
    QCOMPARE(clipboardImage.pixelColor(0, 0), QColor(12, 34, 56));
}

void tst_ImageContextMenu::copyImage_prefersProvidedDisplayImage()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString imagePath = dir.filePath(QStringLiteral("source.png"));
    QImage fileImage(3, 3, QImage::Format_ARGB32);
    fileImage.fill(Qt::red);
    QVERIFY(fileImage.save(imagePath));

    QImage displayedImage(4, 2, QImage::Format_ARGB32);
    displayedImage.fill(Qt::blue);

    QVERIFY(ImageContextMenu::copyImage(imagePath, [displayedImage]() {
        return displayedImage;
    }));

    const QImage clipboardImage = QGuiApplication::clipboard()->image();
    QCOMPARE(clipboardImage.size(), displayedImage.size());
    QCOMPARE(clipboardImage.pixelColor(0, 0), QColor(Qt::blue));
}

void tst_ImageContextMenu::copyImageAddress_copiesAbsoluteNativePath()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString imagePath = dir.filePath(QStringLiteral("source.png"));
    QImage image(2, 2, QImage::Format_ARGB32);
    image.fill(Qt::green);
    QVERIFY(image.save(imagePath));

    QVERIFY(ImageContextMenu::copyImageAddress(imagePath));

    const QString expectedPath = QDir::toNativeSeparators(QFileInfo(imagePath).absoluteFilePath());
    QCOMPARE(QGuiApplication::clipboard()->text(), expectedPath);
}

void tst_ImageContextMenu::menuActions_haveExpectedState()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString imagePath = dir.filePath(QStringLiteral("source.png"));
    QImage image(2, 2, QImage::Format_ARGB32);
    image.fill(Qt::yellow);
    QVERIFY(image.save(imagePath));

    QMenu menu;
    const ImageContextMenu::MenuActions actions = ImageContextMenu::populateMenu(&menu, imagePath);

    QVERIFY(actions.copyImage != nullptr);
    QVERIFY(actions.copyImageAddress != nullptr);
    QVERIFY(actions.openImage != nullptr);
    QVERIFY(actions.revealInFileManager != nullptr);

    QCOMPARE(actions.copyImage->objectName(), QStringLiteral("copyImageAction"));
    QCOMPARE(actions.copyImageAddress->objectName(), QStringLiteral("copyImageAddressAction"));
    QCOMPARE(actions.openImage->objectName(), QStringLiteral("openImageAction"));
    QCOMPARE(actions.revealInFileManager->objectName(),
             QStringLiteral("revealInFileManagerAction"));

    QVERIFY(actions.copyImage->isEnabled());
    QVERIFY(actions.copyImageAddress->isEnabled());
    QVERIFY(actions.openImage->isEnabled());
    QVERIFY(actions.revealInFileManager->isEnabled());

    actions.copyImageAddress->trigger();
    const QString expectedPath = QDir::toNativeSeparators(QFileInfo(imagePath).absoluteFilePath());
    QCOMPARE(QGuiApplication::clipboard()->text(), expectedPath);

    QMenu missingFileMenu;
    const QString missingPath = dir.filePath(QStringLiteral("missing.png"));
    const ImageContextMenu::MenuActions missingActions =
        ImageContextMenu::populateMenu(&missingFileMenu, missingPath);

    QVERIFY(!missingActions.copyImage->isEnabled());
    QVERIFY(missingActions.copyImageAddress->isEnabled());
    QVERIFY(!missingActions.openImage->isEnabled());
    QVERIFY(!missingActions.revealInFileManager->isEnabled());
}

void tst_ImageContextMenu::openAndReveal_rejectMissingFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString missingPath = dir.filePath(QStringLiteral("missing.png"));
    QVERIFY(!ImageContextMenu::openImage(missingPath));
    QVERIFY(!ImageContextMenu::revealInFileManager(missingPath));
}

QTEST_MAIN(tst_ImageContextMenu)
#include "tst_ImageContextMenu.moc"
