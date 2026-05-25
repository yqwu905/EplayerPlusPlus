#include <QTest>
#include <QAction>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QMenu>
#include <QTemporaryDir>

#include <memory>

#include "widgets/ImageContextMenu.h"

class tst_ImageContextMenu : public QObject
{
    Q_OBJECT

private slots:
    void createMenu_enablesActionsForReadableImage();
    void createMenu_disablesFileActionsForMissingImage();
    void copyPathToClipboard_usesNormalizedAbsolutePath();
    void copyImageToClipboard_setsImageData();
    void invalidPath_operationsReturnFalse();

private:
    static QAction *findAction(QMenu *menu, const QString &objectName);
    static QString createImage(QTemporaryDir &dir);
};

QAction *tst_ImageContextMenu::findAction(QMenu *menu, const QString &objectName)
{
    for (QAction *action : menu->actions()) {
        if (action->objectName() == objectName) {
            return action;
        }
    }
    return nullptr;
}

QString tst_ImageContextMenu::createImage(QTemporaryDir &dir)
{
    QImage image(7, 5, QImage::Format_ARGB32);
    image.fill(Qt::green);
    const QString path = dir.filePath(QStringLiteral("image.png"));
    return image.save(path) ? path : QString();
}

void tst_ImageContextMenu::createMenu_enablesActionsForReadableImage()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString imagePath = createImage(dir);
    QVERIFY(!imagePath.isEmpty());

    std::unique_ptr<QMenu> menu(ImageContextMenu::createMenu(imagePath));

    auto *copyImage = findAction(menu.get(), QStringLiteral("copyImageAction"));
    auto *copyPath = findAction(menu.get(), QStringLiteral("copyImagePathAction"));
    auto *openImage = findAction(menu.get(), QStringLiteral("openImageAction"));
    auto *revealImage = findAction(menu.get(), QStringLiteral("revealImageAction"));
    QVERIFY(copyImage != nullptr);
    QVERIFY(copyPath != nullptr);
    QVERIFY(openImage != nullptr);
    QVERIFY(revealImage != nullptr);

    QCOMPARE(copyImage->text(), QStringLiteral("复制图片"));
    QCOMPARE(copyPath->text(), QStringLiteral("复制地址"));
    QCOMPARE(openImage->text(), QStringLiteral("打开图片"));
    QCOMPARE(revealImage->text(), QStringLiteral("在文件浏览器中打开"));
    QVERIFY(copyImage->isEnabled());
    QVERIFY(copyPath->isEnabled());
    QVERIFY(openImage->isEnabled());
    QVERIFY(revealImage->isEnabled());
}

void tst_ImageContextMenu::createMenu_disablesFileActionsForMissingImage()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString missingPath = dir.filePath(QStringLiteral("missing.png"));

    std::unique_ptr<QMenu> menu(ImageContextMenu::createMenu(missingPath));

    auto *copyImage = findAction(menu.get(), QStringLiteral("copyImageAction"));
    auto *copyPath = findAction(menu.get(), QStringLiteral("copyImagePathAction"));
    auto *openImage = findAction(menu.get(), QStringLiteral("openImageAction"));
    auto *revealImage = findAction(menu.get(), QStringLiteral("revealImageAction"));
    QVERIFY(copyImage != nullptr);
    QVERIFY(copyPath != nullptr);
    QVERIFY(openImage != nullptr);
    QVERIFY(revealImage != nullptr);

    QVERIFY(!copyImage->isEnabled());
    QVERIFY(copyPath->isEnabled());
    QVERIFY(!openImage->isEnabled());
    QVERIFY(!revealImage->isEnabled());
}

void tst_ImageContextMenu::copyPathToClipboard_usesNormalizedAbsolutePath()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString imagePath = createImage(dir);
    QVERIFY(!imagePath.isEmpty());

    QVERIFY(ImageContextMenu::copyPathToClipboard(imagePath));
    QCOMPARE(QGuiApplication::clipboard()->text(),
             QDir::toNativeSeparators(
                 QDir::cleanPath(QFileInfo(imagePath).absoluteFilePath())));
}

void tst_ImageContextMenu::copyImageToClipboard_setsImageData()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString imagePath = createImage(dir);
    QVERIFY(!imagePath.isEmpty());

    QVERIFY(ImageContextMenu::copyImageToClipboard(imagePath));
    const QImage copiedImage = QGuiApplication::clipboard()->image();
    QCOMPARE(copiedImage.size(), QSize(7, 5));
    QCOMPARE(copiedImage.pixelColor(0, 0), QColor(Qt::green));
}

void tst_ImageContextMenu::invalidPath_operationsReturnFalse()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString missingPath = dir.filePath(QStringLiteral("missing.png"));

    QVERIFY(!ImageContextMenu::copyImageToClipboard(missingPath));
    QVERIFY(!ImageContextMenu::openImage(missingPath));
    QVERIFY(!ImageContextMenu::revealInFileManager(missingPath));
    QVERIFY(!ImageContextMenu::copyPathToClipboard(QString()));
}

QTEST_MAIN(tst_ImageContextMenu)
#include "tst_ImageContextMenu.moc"
