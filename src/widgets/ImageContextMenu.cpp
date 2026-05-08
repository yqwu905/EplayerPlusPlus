#include "ImageContextMenu.h"

#include <QAction>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QMenu>
#include <QPoint>
#include <QProcess>
#include <QUrl>
#include <QClipboard>

#include <memory>

namespace
{
QString actionText(const char *sourceText)
{
    return QCoreApplication::translate("ImageContextMenu", sourceText);
}

bool isExistingFile(const QString &path)
{
    return !path.isEmpty() && QFileInfo(path).isFile();
}

bool canReadImage(const QString &path)
{
    if (!isExistingFile(path)) {
        return false;
    }

    QImageReader reader(path);
    return reader.canRead();
}
} // namespace

namespace ImageContextMenu
{

QString normalizedImagePath(const QString &imagePath)
{
    if (imagePath.trimmed().isEmpty()) {
        return QString();
    }

    return QDir::cleanPath(QFileInfo(imagePath).absoluteFilePath());
}

bool copyImageToClipboard(const QString &imagePath)
{
    const QString normalizedPath = normalizedImagePath(imagePath);
    if (!isExistingFile(normalizedPath)) {
        return false;
    }

    QImageReader reader(normalizedPath);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        return false;
    }

    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        return false;
    }

    clipboard->setImage(image);
    return true;
}

bool copyPathToClipboard(const QString &imagePath)
{
    const QString normalizedPath = normalizedImagePath(imagePath);
    if (normalizedPath.isEmpty()) {
        return false;
    }

    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        return false;
    }

    clipboard->setText(normalizedPath);
    return true;
}

bool openImage(const QString &imagePath)
{
    const QString normalizedPath = normalizedImagePath(imagePath);
    if (!isExistingFile(normalizedPath)) {
        return false;
    }

    return QDesktopServices::openUrl(QUrl::fromLocalFile(normalizedPath));
}

bool revealInFileManager(const QString &imagePath)
{
    const QString normalizedPath = normalizedImagePath(imagePath);
    if (!isExistingFile(normalizedPath)) {
        return false;
    }

    const QFileInfo fileInfo(normalizedPath);
#if defined(Q_OS_WIN)
    return QProcess::startDetached(
        QStringLiteral("explorer.exe"),
        {QStringLiteral("/select,%1").arg(QDir::toNativeSeparators(normalizedPath))});
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    return QProcess::startDetached(QStringLiteral("open"),
                                   {QStringLiteral("-R"), normalizedPath});
#else
    return QDesktopServices::openUrl(QUrl::fromLocalFile(fileInfo.absolutePath()));
#endif
}

QMenu *createMenu(const QString &imagePath, QWidget *parent)
{
    const QString normalizedPath = normalizedImagePath(imagePath);
    const bool hasPath = !normalizedPath.isEmpty();
    const bool fileExists = isExistingFile(normalizedPath);

    auto *menu = new QMenu(parent);

    QAction *copyImageAction = menu->addAction(actionText("复制图片"));
    copyImageAction->setObjectName(QStringLiteral("copyImageAction"));
    copyImageAction->setEnabled(canReadImage(normalizedPath));
    QObject::connect(copyImageAction, &QAction::triggered, menu, [normalizedPath]() {
        copyImageToClipboard(normalizedPath);
    });

    QAction *copyPathAction = menu->addAction(actionText("复制地址"));
    copyPathAction->setObjectName(QStringLiteral("copyImagePathAction"));
    copyPathAction->setEnabled(hasPath);
    QObject::connect(copyPathAction, &QAction::triggered, menu, [normalizedPath]() {
        copyPathToClipboard(normalizedPath);
    });

    menu->addSeparator();

    QAction *openImageAction = menu->addAction(actionText("打开图片"));
    openImageAction->setObjectName(QStringLiteral("openImageAction"));
    openImageAction->setEnabled(fileExists);
    QObject::connect(openImageAction, &QAction::triggered, menu, [normalizedPath]() {
        openImage(normalizedPath);
    });

    QAction *revealAction = menu->addAction(actionText("在文件浏览器中打开"));
    revealAction->setObjectName(QStringLiteral("revealImageAction"));
    revealAction->setEnabled(fileExists);
    QObject::connect(revealAction, &QAction::triggered, menu, [normalizedPath]() {
        revealInFileManager(normalizedPath);
    });

    return menu;
}

void showMenu(const QString &imagePath, const QPoint &globalPos, QWidget *parent)
{
    if (imagePath.trimmed().isEmpty()) {
        return;
    }

    std::unique_ptr<QMenu> menu(createMenu(imagePath, parent));
    menu->exec(globalPos);
}

} // namespace ImageContextMenu
