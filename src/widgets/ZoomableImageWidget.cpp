#include "ZoomableImageWidget.h"

#include <QPainter>
#include <algorithm>
#include <QCursor>
#include <QEnterEvent>
#include <QFutureWatcher>
#include <QPainterPath>
#include <QtConcurrentRun>
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
    setAttribute(Qt::WA_OpaquePaintEvent);
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
    if (m_magnifierHovering) {
        updateMagnifierHover({});
    }
    update();
}

QPointF ZoomableImageWidget::normalizedPan() const
{
    if (m_image.isNull() || m_image.width() <= 0 || m_image.height() <= 0) {
        return QPointF(0.0, 0.0);
    }
    return QPointF(m_panOffset.x() / m_image.width(),
                   m_panOffset.y() / m_image.height());
}

QRectF ZoomableImageWidget::visibleImageRect() const
{
    if (m_image.isNull() || m_image.width() <= 0 || m_image.height() <= 0) {
        return QRectF();
    }

    const QRectF fullImageRect(0.0, 0.0, m_image.width(), m_image.height());
    if (width() <= 0 || height() <= 0) {
        return fullImageRect;
    }

    QRectF visibleTarget;
    QRectF sourceRect;
    if (!computePaintRects(visibleTarget, sourceRect)) {
        return fullImageRect;
    }

    const QRectF clamped = sourceRect.intersected(fullImageRect);
    return clamped.isEmpty() ? fullImageRect : clamped;
}

void ZoomableImageWidget::setZoomLevel(double level, QPointF focalPoint,
                                        bool emitSignal)
{
    if (m_image.isNull()) {
        m_zoomLevel = qBound(kMinZoom, level, kMaxZoom);
        update();
        if (emitSignal) {
            emit zoomChanged(m_zoomLevel, focalPoint);
        }
        return;
    }

    // focalPoint is in normalized image coordinates [0,1]. Anchor the
    // image pixel currently at that normalized position to its current
    // widget-space position across the zoom change. This mirrors the
    // wheelEvent's "image point under cursor stays under cursor" behaviour.
    const QPointF anchorWidget = normalizedImageToWidget(focalPoint);
    applyZoom(level, anchorWidget);

    if (emitSignal) {
        emit zoomChanged(m_zoomLevel, focalPoint);
    }
}

void ZoomableImageWidget::setNormalizedPan(QPointF normalizedOffset, bool emitSignal)
{
    if (m_image.isNull()) {
        if (emitSignal) {
            emit panChanged(QPointF(0.0, 0.0));
        }
        return;
    }
    m_panOffset = QPointF(normalizedOffset.x() * m_image.width(),
                          normalizedOffset.y() * m_image.height());
    clampPanOffset();
    update();
    if (emitSignal) {
        emit panChanged(normalizedPan());
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

bool ZoomableImageWidget::computePaintRects(QRectF &visibleTarget, QRectF &sourceRect) const
{
    if (m_image.isNull() || m_image.width() <= 0 || m_image.height() <= 0 ||
        width() <= 0 || height() <= 0) {
        return false;
    }

    const double scale = effectiveScale();
    if (scale <= 0.0) {
        return false;
    }

    const double imgW = m_image.width() * scale;
    const double imgH = m_image.height() * scale;

    // Center the image, then apply pan offset (in image pixels, scaled)
    const double ox = (width() - imgW) / 2.0 + m_panOffset.x() * scale;
    const double oy = (height() - imgH) / 2.0 + m_panOffset.y() * scale;

    const QRectF targetRect(ox, oy, imgW, imgH);
    visibleTarget = targetRect.intersected(QRectF(rect()));
    if (visibleTarget.isEmpty()) {
        return false;
    }

    // Explicit source clipping prevents the raster backend from sampling the
    // entire full-resolution image during a deep zoom/pan when only a small
    // viewport-sized region can contribute pixels.
    sourceRect = QRectF((visibleTarget.left() - targetRect.left()) / scale,
                        (visibleTarget.top() - targetRect.top()) / scale,
                        visibleTarget.width() / scale,
                        visibleTarget.height() / scale);
    return true;
}

void ZoomableImageWidget::setMagnifierEnabled(bool enabled)
{
    m_magnifierEnabled = enabled;
    if (enabled && underMouse()) {
        updateMagnifierHover(mapFromGlobal(QCursor::pos()));
    } else if (!enabled) {
        m_magnifierHovering = false;
        setMagnifierAnchor(std::nullopt);
    }
}

void ZoomableImageWidget::setMagnifierAnchor(std::optional<QPointF> normalized)
{
    if (normalized != m_magnifierAnchor) {
        m_magnifierAnchor = normalized;
        update();
    }
}

void ZoomableImageWidget::setMagnifierParameters(double magnification, double size)
{
    m_magnification = qBound(1.5, magnification, 16.0);
    m_magnifierSize = qBound(64.0, size, 400.0);
    update();
}

void ZoomableImageWidget::updateMagnifierHover(const QPointF &position)
{
    QRectF target;
    QRectF source;
    if (computePaintRects(target, source) && target.contains(position)) {
        m_magnifierHovering = true;
        const QPointF normalized = widgetToNormalized(position);
        setMagnifierAnchor(normalized);
        emit magnifierAnchorChanged(normalized, true);
    } else if (m_magnifierHovering) {
        m_magnifierHovering = false;
        setMagnifierAnchor(std::nullopt);
        emit magnifierAnchorChanged({}, false);
    }
}

void ZoomableImageWidget::enterEvent(QEnterEvent *event)
{
    if (m_magnifierEnabled) {
        updateMagnifierHover(event->position());
    }
    QWidget::enterEvent(event);
}

void ZoomableImageWidget::leaveEvent(QEvent *event)
{
    if (m_magnifierHovering) {
        m_magnifierHovering = false;
        setMagnifierAnchor(std::nullopt);
        emit magnifierAnchorChanged({}, false);
    }
    QWidget::leaveEvent(event);
}

void ZoomableImageWidget::setImageInformationVisible(bool visible)
{
    m_informationVisible = visible;
    requestImageInformation();
    update();
}

void ZoomableImageWidget::setImageInformationSource(const QString &path, const QImage &original)
{
    if (path == m_informationPath && original.cacheKey() == m_informationSource.cacheKey()) {
        return;
    }
    m_informationPath = path;
    m_informationSource = original;
    m_information.reset();
    requestImageInformation();
    update();
}

void ZoomableImageWidget::requestImageInformation()
{
    if (!m_informationVisible || m_informationWatcher ||
        m_information || m_informationSource.isNull()) {
        return;
    }

    const QString path = m_informationPath;
    const QImage image = m_informationSource;
    auto *watcher = new QFutureWatcher<ImageInformation>(this);
    m_informationWatcher = watcher;
    connect(watcher, &QFutureWatcher<ImageInformation>::finished, this, [this, watcher, path, image] {
        m_informationWatcher = nullptr;
        if (m_informationPath == path && m_informationSource.cacheKey() == image.cacheKey()) {
            m_information = watcher->result();
            update();
        }
        watcher->deleteLater();
        requestImageInformation();
    });
    watcher->setFuture(QtConcurrent::run([path, image] { return readImageInformation(path, image); }));
}

void ZoomableImageWidget::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.fillRect(rect(), palette().alternateBase());

    if (m_image.isNull()) {
        // Draw placeholder text
        if (!m_text.isEmpty()) {
            painter.setPen(palette().color(QPalette::PlaceholderText));
            QFont font = painter.font();
            font.setPixelSize(12);
            painter.setFont(font);
            painter.drawText(rect(), Qt::AlignCenter, m_text);
        }
        return;
    }

    QRectF visibleTarget;
    QRectF sourceRect;
    if (!computePaintRects(visibleTarget, sourceRect)) {
        return;
    }
    painter.drawImage(visibleTarget, m_image, sourceRect);

    if (m_informationVisible && m_information) {
        paintImageInformation(painter, visibleTarget);
    }
    if (m_magnifierEnabled && m_magnifierAnchor) {
        paintMagnifier(painter, visibleTarget, sourceRect);
    }
}

void ZoomableImageWidget::paintMagnifier(QPainter &painter, const QRectF &visibleTarget,
                                        const QRectF &sourceRect)
{
    constexpr qreal gap = 14;
    const qreal side = qMin(m_magnifierSize, qreal(qMin(width(), height()) - 4));
    if (side <= 0) {
        return;
    }

    const QPointF imagePoint(m_magnifierAnchor->x() * m_image.width(),
                             m_magnifierAnchor->y() * m_image.height());
    const qreal scale = effectiveScale();
    const QPointF position = visibleTarget.topLeft() + (imagePoint - sourceRect.topLeft()) * scale;
    const QPointF cursor(qBound(visibleTarget.left(), position.x(), visibleTarget.right()),
                         qBound(visibleTarget.top(), position.y(), visibleTarget.bottom()));
    const qreal lensScale = scale * m_magnification;
    const qreal sampleSide = side / lensScale;
    const QRectF sample(imagePoint - QPointF(sampleSide / 2, sampleSide / 2),
                        QSizeF(sampleSide, sampleSide));

    const qreal x = cursor.x() + gap + side <= width() ? cursor.x() + gap : cursor.x() - gap - side;
    const qreal y = cursor.y() + gap + side <= height() ? cursor.y() + gap : cursor.y() - gap - side;
    const QRectF lens(qBound(2.0, x, width() - side - 2),
                      qBound(2.0, y, height() - side - 2), side, side);
    const QRectF clipped = sample.intersected(QRectF(m_image.rect()));
    const QRectF target(lens.topLeft() + (clipped.topLeft() - sample.topLeft()) * lensScale,
                        clipped.size() * lensScale);

    painter.fillRect(lens, palette().alternateBase());
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(target, m_image, clipped);
    painter.setPen(QPen(palette().color(QPalette::WindowText), 2));
    painter.drawRect(lens);
}

void ZoomableImageWidget::paintImageInformation(QPainter &painter, const QRectF &visibleTarget) const
{
    constexpr qreal margin = 8;
    if (visibleTarget.width() < 100 || visibleTarget.height() < 75) {
        return;
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    QFont small = painter.font();
    small.setPixelSize(11);
    painter.setFont(small);
    const QFontMetrics metrics(small);
    const auto &info = *m_information;
    const QStringList lines = info.exifLines.isEmpty()
        ? QStringList{tr("无 EXIF 信息")} : info.exifLines;

    const qreal histogramWidth = qMin(176.0, visibleTarget.width() - 2 * margin);
    const qreal histogramHeight = qMin(88.0, visibleTarget.height() * 0.40);
    const qreal exifWidth = qMin(220.0, visibleTarget.width() - 2 * margin);
    const bool sharesWidth = exifWidth + histogramWidth + 3 * margin > visibleTarget.width();
    const qreal exifHeightLimit = visibleTarget.height() - 2 * margin
        - (sharesWidth ? histogramHeight + margin : 0);
    const qreal exifHeight = qMin(metrics.height() * (lines.size() + 1) + 12.0,
                                  exifHeightLimit);
    const QRectF exif(visibleTarget.right() - exifWidth - margin, visibleTarget.top() + margin,
                      exifWidth, exifHeight);
    const auto drawPanel = [&painter](const QRectF &panel) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(14, 19, 26, 195));
        painter.drawRoundedRect(panel, 6, 6);
    };
    drawPanel(exif);
    painter.save();
    painter.setClipRect(exif);
    painter.setPen(Qt::white);
    qreal y = exif.top() + 6 + metrics.ascent();
    painter.drawText(QPointF(exif.left() + 8, y), QStringLiteral("EXIF"));
    for (const QString &line : lines) {
        y += metrics.height();
        painter.drawText(QPointF(exif.left() + 8, y),
                         metrics.elidedText(line, Qt::ElideRight, qRound(exif.width() - 16)));
    }
    painter.restore();

    const QRectF histogram(visibleTarget.left() + margin,
                           visibleTarget.bottom() - histogramHeight - margin,
                           histogramWidth, histogramHeight);
    drawPanel(histogram);
    painter.setPen(Qt::white);
    painter.drawText(QPointF(histogram.left() + 8, histogram.top() + 6 + metrics.ascent()),
                     tr("RGB 直方图"));
    const QRectF plot = histogram.adjusted(8, metrics.height() + 9, -8, -7);
    quint64 maximum = 0;
    for (const auto &channel : info.histogram) {
        maximum = qMax(maximum, *std::max_element(channel.begin(), channel.end()));
    }
    if (maximum > 0) {
        const QColor colors[] = {QColor(255, 108, 108), QColor(100, 235, 150), QColor(117, 174, 255)};
        for (int channel = 0; channel < 3; ++channel) {
            QPainterPath curve(QPointF(plot.left(), plot.bottom()));
            for (int bin = 0; bin < 256; ++bin) {
                curve.lineTo(plot.left() + plot.width() * bin / 255,
                             plot.bottom() - plot.height() * qreal(info.histogram[channel][bin]) / maximum);
            }
            painter.setPen(colors[channel]);
            painter.drawPath(curve);
        }
    }
    painter.restore();
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

QPointF ZoomableImageWidget::normalizedImageToWidget(const QPointF &normalizedImage) const
{
    if (m_image.isNull()) {
        return QPointF(width() / 2.0, height() / 2.0);
    }
    const double scale = effectiveScale();
    const double imgW = m_image.width() * scale;
    const double imgH = m_image.height() * scale;
    const double ox = (width() - imgW) / 2.0 + m_panOffset.x() * scale;
    const double oy = (height() - imgH) / 2.0 + m_panOffset.y() * scale;
    return QPointF(ox + normalizedImage.x() * imgW,
                   oy + normalizedImage.y() * imgH);
}

void ZoomableImageWidget::applyZoom(double newLevel, const QPointF &anchorWidget)
{
    if (m_image.isNull()) {
        m_zoomLevel = qBound(kMinZoom, newLevel, kMaxZoom);
        update();
        return;
    }

    // Image coordinate under the anchor before the zoom change.
    const double oldScale = effectiveScale();
    const double oldImgW = m_image.width() * oldScale;
    const double oldImgH = m_image.height() * oldScale;
    const double oldOx = (width() - oldImgW) / 2.0 + m_panOffset.x() * oldScale;
    const double oldOy = (height() - oldImgH) / 2.0 + m_panOffset.y() * oldScale;
    const double imgX = (anchorWidget.x() - oldOx) / oldScale;
    const double imgY = (anchorWidget.y() - oldOy) / oldScale;

    // Apply new zoom (clamped).
    m_zoomLevel = qBound(kMinZoom, newLevel, kMaxZoom);

    // Solve for the pan that keeps (imgX, imgY) under anchorWidget.
    // anchorWidget = newOxNoPan + (m_panOffset + (imgX, imgY)) * newScale
    const double newScale = effectiveScale();
    const double newImgW = m_image.width() * newScale;
    const double newImgH = m_image.height() * newScale;
    const double newOxNoPan = (width() - newImgW) / 2.0;
    const double newOyNoPan = (height() - newImgH) / 2.0;
    const double newPanX = (anchorWidget.x() - newOxNoPan - imgX * newScale) / newScale;
    const double newPanY = (anchorWidget.y() - newOyNoPan - imgY * newScale) / newScale;
    m_panOffset = QPointF(newPanX, newPanY);

    // At fit-to-view, snap pan back to zero so the image stays centred.
    if (qFuzzyCompare(m_zoomLevel, 1.0)) {
        m_panOffset = QPointF(0.0, 0.0);
    }

    clampPanOffset();
    update();
}

void ZoomableImageWidget::wheelEvent(QWheelEvent *event)
{
    if (m_image.isNull()) {
        event->ignore();
        return;
    }

    const double steps = event->angleDelta().y() / 120.0;
    const auto modifiers = event->modifiers();
    if (m_magnifierEnabled && (modifiers == Qt::AltModifier || modifiers == Qt::ControlModifier)) {
        updateMagnifierHover(event->position());
        if (m_magnifierHovering) {
            const double factor = qPow(1.2, qBound(-32.0, steps, 32.0));
            setMagnifierParameters(modifiers == Qt::AltModifier ? m_magnification * factor : m_magnification,
                                   modifiers == Qt::ControlModifier ? m_magnifierSize * factor : m_magnifierSize);
            emit magnifierParametersChanged(m_magnification, m_magnifierSize);
            event->accept();
            return;
        }
    }

    const QPointF focalWidget = event->position();
    // Capture the focal point in normalized image coords *before* zoom so the
    // emitted signal lets linked cells anchor on the same image-relative point.
    const QPointF normalizedFocal = widgetToNormalized(focalWidget);

    // angleDelta is in eighths of a degree; a standard mouse-wheel detent is
    // 15 degrees == 120 units. Using the continuous value (a double) instead of
    // an integer 120-step count lets high-resolution trackpads and precision
    // wheels — which report deltas far smaller than 120 — zoom proportionally
    // instead of being truncated to zero (no zoom at all on a macOS trackpad).
    // A full detent still yields exactly one kZoomStep, so ordinary mouse-wheel
    // behaviour is unchanged.
    const double factor = qPow(kZoomStep, steps);
    const double newLevel = m_zoomLevel * factor;

    applyZoom(newLevel, focalWidget);

    emit zoomChanged(m_zoomLevel, normalizedFocal);
    if (m_magnifierEnabled) {
        updateMagnifierHover(focalWidget);
    }
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

        // Emit normalized pan so linked cells with differently-sized images
        // stay in agreement on which image region is centred.
        emit panChanged(normalizedPan());
        if (m_magnifierEnabled) {
            updateMagnifierHover(event->position());
        }
        event->accept();
        return;
    }

    if (m_magnifierEnabled) {
        updateMagnifierHover(event->position());
    }

    // Change cursor when hoverable for pan. Only call setCursor when the shape
    // actually changes: this slot runs on every mouse move, and reassigning the
    // cursor each time fights any context-driven cursor and can flicker.
    const Qt::CursorShape desired = (m_zoomLevel > 1.0)
        ? Qt::OpenHandCursor
        : Qt::ArrowCursor;
    if (cursor().shape() != desired) {
        setCursor(desired);
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
