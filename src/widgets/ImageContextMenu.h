#ifndef IMAGECONTEXTMENU_H
#define IMAGECONTEXTMENU_H

#include <QString>

class QMenu;
class QPoint;
class QWidget;

namespace ImageContextMenu
{

QString normalizedImagePath(const QString &imagePath);
bool copyImageToClipboard(const QString &imagePath);
bool copyPathToClipboard(const QString &imagePath);
bool openImage(const QString &imagePath);
bool revealInFileManager(const QString &imagePath);

QMenu *createMenu(const QString &imagePath, QWidget *parent = nullptr);
void showMenu(const QString &imagePath, const QPoint &globalPos, QWidget *parent = nullptr);

} // namespace ImageContextMenu

#endif // IMAGECONTEXTMENU_H
