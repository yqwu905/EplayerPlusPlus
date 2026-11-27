#include "ThumbnailWidget.h"
#include "ImageContextMenu.h"

#include <QContextMenuEvent>
#include <QPainter>
#include <QMouseEvent>
#include <QFontMetrics>
#include <QEnterEvent>
#include <QPainterPath>

ThumbnailWidget::ThumbnailWidget(QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(sizeHint());
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    setMouseTracking(true);
}

ThumbnailWidget::~ThumbnailWidget() = default;

void ThumbnailWidget::setThumbnail(const QImage &thumbnail)
{
    if (thumbnail.isNull()) {
        m_pixmap = QPixmap();
    } else {
        // Thumbnail is already generated at the correct size by
        // ImageUtils::generateThumbnail — no redundant scaling needed.
        m_pixmap = QPixmap::fromImage(thumbnail);
    }
    update();
}

void ThumbnailWidget::setFileName(const QString &fileName)
{
    m_fileName = fileName;
    update();
}

void ThumbnailWidget::setFilePath(const QString &filePath)
{
    m_filePath = filePath;
}

void ThumbnailWidget::setSelected(bool selected)
{
    if (m_selected != selected) {
        m_selected = selected;
        update();
    }
}

void ThumbnailWidget::setThumbnailSize(const QSize &size)
{
    m_thumbnailSize = size;
    setFixedSize(sizeHint());
    update();
}

QSize ThumbnailWidget::sizeHint() const
{
    return QSize(
        m_thumbnailSize.width() + 2 * kPadding + 2 * kSelectionBorder,
        m_thumbnailSize.height() + kTextHeight + 3 * kPadding + 2 * kSelectionBorder
    );
}

void ThumbnailWidget::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QRect widgetRect = rect();
    QRect cardRect = widgetRect.adjusted(1, 1, -1, -1);

    // ---- Fluent 2 card styling ----
    if (m_selected) {
        // Selected: brand accent border and tinted fill
        painter.setPen(QPen(QColor(0x00, 0x78, 0xD4), 2));       // brandPrimary
        painter.setBrush(palette().midlight().color());
        painter.drawRoundedRect(cardRect, 8, 8);
    } else if (m_hovered) {
        // Hover: subtle elevation — slightly darker bg + thin border
        painter.setPen(QPen(palette().dark().color(), 1));
        painter.setBrush(palette().alternateBase().color());
        painter.drawRoundedRect(cardRect, 8, 8);

        // Shadow simulation (2 pixel offset soft line at bottom)
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 12));
        painter.drawRoundedRect(cardRect.adjusted(2, 2, -2, 0), 8, 8);
        // Re-draw the card surface on top
        painter.setPen(QPen(palette().dark().color(), 1));
        painter.setBrush(palette().base().color());
        painter.drawRoundedRect(cardRect, 8, 8);
    } else {
        // Normal: plain card surface with a subtle border
        painter.setPen(QPen(palette().mid().color(), 1));
        painter.setBrush(palette().base().color());
        painter.drawRoundedRect(cardRect, 8, 8);
    }

    // ---- Thumbnail area with rounded clip ----
    QRect thumbArea(
        kPadding + kSelectionBorder,
        kPadding + kSelectionBorder,
        m_thumbnailSize.width(),
        m_thumbnailSize.height()
    );

    if (!m_pixmap.isNull()) {
        // Clip to rounded top of card
        QPainterPath clipPath;
        clipPath.addRoundedRect(thumbArea, 6, 6);
        painter.setClipPath(clipPath);

        // Center the pixmap within the thumbnail area
        int x = thumbArea.x() + (thumbArea.width() - m_pixmap.width()) / 2;
        int y = thumbArea.y() + (thumbArea.height() - m_pixmap.height()) / 2;

        // Light background for image area
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette().alternateBase().color());
        painter.drawRect(thumbArea);

        painter.drawPixmap(x, y, m_pixmap);
        painter.setClipping(false);
    } else {
        // Placeholder
        QPainterPath clipPath;
        clipPath.addRoundedRect(thumbArea, 6, 6);
        painter.setClipPath(clipPath);

        painter.setPen(Qt::NoPen);
        painter.setBrush(palette().alternateBase().color());
        painter.drawRect(thumbArea);

        painter.setPen(palette().color(QPalette::PlaceholderText));
        QFont placeholderFont = painter.font();
        placeholderFont.setPointSize(10);
        painter.setFont(placeholderFont);
        painter.drawText(thumbArea, Qt::AlignCenter, tr("Loading..."));
        painter.setClipping(false);
    }

    // ---- Filename text — Fluent 2 caption style ----
    QRect textArea(
        kPadding + kSelectionBorder,
        thumbArea.bottom() + kPadding,
        m_thumbnailSize.width(),
        kTextHeight
    );

    painter.setPen(palette().buttonText().color());
    QFont font = painter.font();
    font.setPointSize(10);
    painter.setFont(font);

    QFontMetrics fm(font);
    QString elidedText = fm.elidedText(m_fileName, Qt::ElideMiddle, textArea.width());
    painter.drawText(textArea, Qt::AlignCenter, elidedText);
}

void ThumbnailWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit clicked(m_filePath, event->modifiers());
    }
    QWidget::mousePressEvent(event);
}

void ThumbnailWidget::contextMenuEvent(QContextMenuEvent *event)
{
    if (!m_filePath.isEmpty()) {
        ImageContextMenu::showMenu(m_filePath, event->globalPos(), this);
        event->accept();
        return;
    }

    QWidget::contextMenuEvent(event);
}

void ThumbnailWidget::enterEvent(QEnterEvent * /*event*/)
{
    m_hovered = true;
    update();
}

void ThumbnailWidget::leaveEvent(QEvent * /*event*/)
{
    m_hovered = false;
    update();
}
