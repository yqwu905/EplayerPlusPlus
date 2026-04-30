#include "ImageContextMenu.h"

#include <QAction>
#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QMenu>
#include <QProcess>
#include <QUrl>

namespace
{

QString actionText(const char *sourceText)
{
    return QCoreApplication::translate("ImageContextMenu", sourceText);
}

QFileInfo fileInfoForPath(const QString &imagePath)
{
    return QFileInfo(imagePath);
}

bool isExistingImageFile(const QString &imagePath)
{
    const QFileInfo info = fileInfoForPath(imagePath);
    return info.exists() && info.isFile();
}

QString absolutePathForClipboard(const QString &imagePath)
{
    const QFileInfo info = fileInfoForPath(imagePath);
    const QString absolute = info.absoluteFilePath();
    return absolute.isEmpty() ? QString() : QDir::toNativeSeparators(absolute);
}

QImage loadImageFromFile(const QString &imagePath)
{
    if (!isExistingImageFile(imagePath)) {
        return QImage();
    }

    QImageReader reader(imagePath);
    reader.setAutoTransform(true);
    return reader.read();
}

} // namespace

namespace ImageContextMenu
{

bool copyImage(const QString &imagePath, ImageProvider imageProvider)
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        return false;
    }

    QImage image;
    if (imageProvider) {
        image = imageProvider();
    }
    if (image.isNull()) {
        image = loadImageFromFile(imagePath);
    }
    if (image.isNull()) {
        return false;
    }

    clipboard->setImage(image, QClipboard::Clipboard);
    return true;
}

bool copyImageAddress(const QString &imagePath)
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        return false;
    }

    const QString clipboardPath = absolutePathForClipboard(imagePath);
    if (clipboardPath.isEmpty()) {
        return false;
    }

    clipboard->setText(clipboardPath, QClipboard::Clipboard);
    return true;
}

bool openImage(const QString &imagePath)
{
    if (!isExistingImageFile(imagePath)) {
        return false;
    }

    return QDesktopServices::openUrl(
        QUrl::fromLocalFile(fileInfoForPath(imagePath).absoluteFilePath()));
}

bool revealInFileManager(const QString &imagePath)
{
    const QFileInfo info = fileInfoForPath(imagePath);
    if (!info.exists()) {
        return false;
    }

#if defined(Q_OS_WIN)
    return QProcess::startDetached(QStringLiteral("explorer.exe"),
                                   QStringList{QStringLiteral("/select,"),
                                               QDir::toNativeSeparators(info.absoluteFilePath())});
#elif defined(Q_OS_MACOS)
    return QProcess::startDetached(QStringLiteral("open"),
                                   QStringList{QStringLiteral("-R"), info.absoluteFilePath()});
#else
    const QString folderPath = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    return QDesktopServices::openUrl(QUrl::fromLocalFile(folderPath));
#endif
}

MenuActions populateMenu(QMenu *menu,
                         const QString &imagePath,
                         ImageProvider imageProvider)
{
    MenuActions actions;
    if (!menu) {
        return actions;
    }

    const bool hasPath = !imagePath.isEmpty();
    const bool hasFile = isExistingImageFile(imagePath);

    actions.copyImage = menu->addAction(actionText("Copy Image"));
    actions.copyImage->setObjectName(QStringLiteral("copyImageAction"));
    actions.copyImage->setEnabled(hasFile || static_cast<bool>(imageProvider));

    actions.copyImageAddress = menu->addAction(actionText("Copy Image Address"));
    actions.copyImageAddress->setObjectName(QStringLiteral("copyImageAddressAction"));
    actions.copyImageAddress->setEnabled(hasPath);

    menu->addSeparator();

    actions.openImage = menu->addAction(actionText("Open Image"));
    actions.openImage->setObjectName(QStringLiteral("openImageAction"));
    actions.openImage->setEnabled(hasFile);

    actions.revealInFileManager = menu->addAction(actionText("Show in File Manager"));
    actions.revealInFileManager->setObjectName(QStringLiteral("revealInFileManagerAction"));
    actions.revealInFileManager->setEnabled(hasFile);

    QObject::connect(actions.copyImage, &QAction::triggered, menu,
                     [imagePath, imageProvider]() {
        copyImage(imagePath, imageProvider);
    });
    QObject::connect(actions.copyImageAddress, &QAction::triggered, menu,
                     [imagePath]() {
        copyImageAddress(imagePath);
    });
    QObject::connect(actions.openImage, &QAction::triggered, menu,
                     [imagePath]() {
        openImage(imagePath);
    });
    QObject::connect(actions.revealInFileManager, &QAction::triggered, menu,
                     [imagePath]() {
        revealInFileManager(imagePath);
    });

    return actions;
}

void showMenu(QWidget *parent,
              const QPoint &globalPos,
              const QString &imagePath,
              ImageProvider imageProvider)
{
    QMenu menu(parent);
    populateMenu(&menu, imagePath, imageProvider);
    menu.exec(globalPos);
}

} // namespace ImageContextMenu
