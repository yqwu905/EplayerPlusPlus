#include "ZoomableImageWidget.h"

#include <QPainter>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QResizeEvent>

ZoomableImageWidget::ZoomableImageWidget(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(200, 200);
}

ZoomableImageWidget::~ZoomableImageWidget() = default;

void ZoomableImageWidget::setImage(const QImage &image, bool resetViewState)
{
    m_image = image;
    m_text.clear();
    if (resetViewState) {
        // Reset view to fit-to-view so the image fills the grid cell
        m_zoomLevel = 1.0;
        m_panOffset = QPointF(0.0, 0.0);
    } else {
        // Keep current zoom/pan, just re-clamp in case the new image has
        // different dimensions
        clampPanOffset();
    }
    update();
}

void ZoomableImageWidget::setImage(const QPixmap &pixmap, bool resetViewState)
{
    setImage(pixmap.toImage(), resetViewState);
}

void ZoomableImageWidget::setText(const QString &text)
{
    m_text = text;
    m_image = QImage();
    update();
}

void ZoomableImageWidget::setZoomLevel(double level, QPointF focalPoint,
                                        bool emitSignal)
{
    Q_UNUSED(focalPoint);
    m_zoomLevel = qBound(kMinZoom, level, kMaxZoom);
    clampPanOffset();
    update();
    if (emitSignal) {
        emit zoomChanged(m_zoomLevel, focalPoint);
    }
}

void ZoomableImageWidget::setPanOffset(QPointF offset, bool emitSignal)
{
    m_panOffset = offset;
    clampPanOffset();
    update();
    if (emitSignal) {
        emit panChanged(m_panOffset);
    }
}

void ZoomableImageWidget::resetView(bool emitSignal)
{
    m_zoomLevel = 1.0;
    m_panOffset = QPointF(0.0, 0.0);
    update();
    if (emitSignal) {
        emit viewReset();
    }
}

// ---- Painting ----

double ZoomableImageWidget::fitScale() const
{
    if (m_image.isNull()) return 1.0;

    double scaleX = static_cast<double>(width()) / m_image.width();
    double scaleY = static_cast<double>(height()) / m_image.height();
    return qMin(scaleX, scaleY);
}

double ZoomableImageWidget::effectiveScale() const
{
    return fitScale() * m_zoomLevel;
}

void ZoomableImageWidget::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.fillRect(rect(), QColor(0xF5, 0xF5, 0xF5));

    if (m_image.isNull()) {
        // Draw placeholder text
        if (!m_text.isEmpty()) {
            painter.setPen(QColor(0x9E, 0x9E, 0x9E));
            QFont font = painter.font();
            font.setPixelSize(12);
            painter.setFont(font);
            painter.drawText(rect(), Qt::AlignCenter, m_text);
        }
        return;
    }

    double scale = effectiveScale();
    double imgW = m_image.width() * scale;
    double imgH = m_image.height() * scale;

    // Center the image, then apply pan offset (in image pixels, scaled)
    double ox = (width() - imgW) / 2.0 + m_panOffset.x() * scale;
    double oy = (height() - imgH) / 2.0 + m_panOffset.y() * scale;

    painter.translate(ox, oy);
    painter.scale(scale, scale);
    painter.drawImage(0, 0, m_image);
}

// ---- Mouse interaction ----

QPointF ZoomableImageWidget::widgetToNormalized(const QPointF &widgetPos) const
{
    if (m_image.isNull()) return QPointF(0.5, 0.5);

    double scale = effectiveScale();
    double imgW = m_image.width() * scale;
    double imgH = m_image.height() * scale;
    double ox = (width() - imgW) / 2.0 + m_panOffset.x() * scale;
    double oy = (height() - imgH) / 2.0 + m_panOffset.y() * scale;

    double nx = (widgetPos.x() - ox) / imgW;
    double ny = (widgetPos.y() - oy) / imgH;
    return QPointF(qBound(0.0, nx, 1.0), qBound(0.0, ny, 1.0));
}

void ZoomableImageWidget::clampPanOffset()
{
    if (m_image.isNull()) return;

    // Allow panning up to half the image dimension beyond edges
    double maxPanX = m_image.width() * 0.5;
    double maxPanY = m_image.height() * 0.5;
    m_panOffset.setX(qBound(-maxPanX, m_panOffset.x(), maxPanX));
    m_panOffset.setY(qBound(-maxPanY, m_panOffset.y(), maxPanY));
}

void ZoomableImageWidget::wheelEvent(QWheelEvent *event)
{
    if (m_image.isNull()) {
        event->ignore();
        return;
    }

    // Calculate focal point in image coordinates before zoom
    QPointF focalWidget = event->position();
    double oldScale = effectiveScale();
    double oldImgW = m_image.width() * oldScale;
    double oldImgH = m_image.height() * oldScale;
    double oldOx = (width() - oldImgW) / 2.0 + m_panOffset.x() * oldScale;
    double oldOy = (height() - oldImgH) / 2.0 + m_panOffset.y() * oldScale;

    // Image coordinate under cursor before zoom
    double imgX = (focalWidget.x() - oldOx) / oldScale;
    double imgY = (focalWidget.y() - oldOy) / oldScale;

    // Compute new zoom
    int steps = event->angleDelta().y() / 120;
    double factor = qPow(kZoomStep, steps);
    m_zoomLevel = qBound(kMinZoom, m_zoomLevel * factor, kMaxZoom);

    // Adjust pan so the same image point stays under the cursor
    double newScale = effectiveScale();
    // New widget position of the image point (without pan adjustment)
    double newImgW = m_image.width() * newScale;
    double newImgH = m_image.height() * newScale;
    double newOxNoPan = (width() - newImgW) / 2.0;
    double newOyNoPan = (height() - newImgH) / 2.0;

    // We want: focalWidget.x() == newOxNoPan + m_panOffset.x() * newScale + imgX * newScale
    double newPanX = (focalWidget.x() - newOxNoPan - imgX * newScale) / newScale;
    double newPanY = (focalWidget.y() - newOyNoPan - imgY * newScale) / newScale;
    m_panOffset = QPointF(newPanX, newPanY);

    // If zoomed back to 1.0, reset pan
    if (qFuzzyCompare(m_zoomLevel, 1.0)) {
        m_panOffset = QPointF(0, 0);
    }

    clampPanOffset();
    update();

    QPointF normalizedFocal = widgetToNormalized(focalWidget);
    emit zoomChanged(m_zoomLevel, normalizedFocal);

    event->accept();
}

void ZoomableImageWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_zoomLevel > 1.0) {
        m_isPanning = true;
        m_lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ZoomableImageWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isPanning) {
        QPoint delta = event->pos() - m_lastMousePos;
        m_lastMousePos = event->pos();

        double scale = effectiveScale();
        // Convert pixel delta to image coordinate delta
        m_panOffset += QPointF(delta.x() / scale, delta.y() / scale);
        clampPanOffset();
        update();

        emit panChanged(m_panOffset);
        event->accept();
        return;
    }

    // Change cursor when hoverable for pan
    if (m_zoomLevel > 1.0 && !m_isPanning) {
        setCursor(Qt::OpenHandCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }

    QWidget::mouseMoveEvent(event);
}

void ZoomableImageWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_isPanning) {
        m_isPanning = false;
        if (m_zoomLevel > 1.0) {
            setCursor(Qt::OpenHandCursor);
        } else {
            setCursor(Qt::ArrowCursor);
        }
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ZoomableImageWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        resetView(true); // emitSignal = true
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void ZoomableImageWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // Clamp pan on resize to keep image in bounds
    clampPanOffset();
    update();
}
