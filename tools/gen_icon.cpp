/**
 * @brief Utility script to generate the application icon.
 * Run this once to create the icon file, then it can be removed.
 */
#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QDir>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QImage icon(64, 64, QImage::Format_ARGB32);
    icon.fill(Qt::transparent);

    QPainter painter(&icon);
    painter.setRenderHint(QPainter::Antialiasing);

    // Left image frame (blue)
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 120, 215));
    painter.drawRoundedRect(4, 8, 26, 48, 3, 3);

    // Right image frame (green)
    painter.setBrush(QColor(60, 180, 75));
    painter.drawRoundedRect(34, 8, 26, 48, 3, 3);

    // Comparison arrow right
    QPolygonF rightArrow;
    rightArrow << QPointF(26, 24) << QPointF(38, 32) << QPointF(26, 40);
    painter.setBrush(Qt::white);
    painter.drawPolygon(rightArrow);

    painter.end();

    QString path = QDir::currentPath() + "/resources/icons/app_icon.png";
    icon.save(path);
    qDebug() << "Icon saved to:" << path;

    return 0;
}
