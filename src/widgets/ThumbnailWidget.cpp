#include "ThumbnailWidget.h"

#include <QPainter>
#include <QMouseEvent>
#include <QFontMetrics>

ThumbnailWidget::ThumbnailWidget(QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(sizeHint());
    setCursor(Qt::PointingHandCursor);
}

ThumbnailWidget::~ThumbnailWidget() = default;

void ThumbnailWidget::setThumbnail(const QImage &thumbnail)
{
    if (thumbnail.isNull()) {
        m_pixmap = QPixmap();
    } else {
        m_pixmap = QPixmap::fromImage(thumbnail).scaled(
            m_thumbnailSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
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

    // Selection highlight background
    if (m_selected) {
        painter.setPen(QPen(QColor(0, 120, 215), kSelectionBorder));
        painter.setBrush(QColor(0, 120, 215, 30));
        painter.drawRoundedRect(widgetRect.adjusted(1, 1, -1, -1), 4, 4);
    } else {
        // Subtle border
        painter.setPen(QPen(QColor(200, 200, 200), 1));
        painter.setBrush(QColor(245, 245, 245));
        painter.drawRoundedRect(widgetRect.adjusted(1, 1, -1, -1), 4, 4);
    }

    // Thumbnail area
    QRect thumbArea(
        kPadding + kSelectionBorder,
        kPadding + kSelectionBorder,
        m_thumbnailSize.width(),
        m_thumbnailSize.height()
    );

    if (!m_pixmap.isNull()) {
        // Center the pixmap within the thumbnail area
        int x = thumbArea.x() + (thumbArea.width() - m_pixmap.width()) / 2;
        int y = thumbArea.y() + (thumbArea.height() - m_pixmap.height()) / 2;
        painter.drawPixmap(x, y, m_pixmap);
    } else {
        // Placeholder
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(230, 230, 230));
        painter.drawRect(thumbArea);

        painter.setPen(QColor(180, 180, 180));
        painter.drawText(thumbArea, Qt::AlignCenter, tr("Loading..."));
    }

    // Filename text
    QRect textArea(
        kPadding + kSelectionBorder,
        thumbArea.bottom() + kPadding,
        m_thumbnailSize.width(),
        kTextHeight
    );

    painter.setPen(Qt::black);
    QFont font = painter.font();
    font.setPointSize(9);
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
