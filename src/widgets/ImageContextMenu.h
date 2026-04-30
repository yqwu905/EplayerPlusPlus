#ifndef IMAGECONTEXTMENU_H
#define IMAGECONTEXTMENU_H

#include <QImage>
#include <QPoint>
#include <QString>

#include <functional>

class QAction;
class QMenu;
class QWidget;

namespace ImageContextMenu
{

using ImageProvider = std::function<QImage()>;

struct MenuActions {
    QAction *copyImage = nullptr;
    QAction *copyImageAddress = nullptr;
    QAction *openImage = nullptr;
    QAction *revealInFileManager = nullptr;
};

bool copyImage(const QString &imagePath, ImageProvider imageProvider = ImageProvider());
bool copyImageAddress(const QString &imagePath);
bool openImage(const QString &imagePath);
bool revealInFileManager(const QString &imagePath);

MenuActions populateMenu(QMenu *menu,
                         const QString &imagePath,
                         ImageProvider imageProvider = ImageProvider());
void showMenu(QWidget *parent,
              const QPoint &globalPos,
              const QString &imagePath,
              ImageProvider imageProvider = ImageProvider());

} // namespace ImageContextMenu

#endif // IMAGECONTEXTMENU_H
