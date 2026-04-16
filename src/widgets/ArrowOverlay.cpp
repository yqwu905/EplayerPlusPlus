#include "ArrowOverlay.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainterPath>

ArrowOverlay::ArrowOverlay(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setMouseTracking(true);
}

ArrowOverlay::~ArrowOverlay() = default;

void ArrowOverlay::setDirections(const QList<Direction> &directions)
{
    // Preserve any target indices that were set before setDirections
    QHash<Direction, int> savedTargets;
    for (const auto &arrow : m_arrows) {
        if (arrow.targetIndex >= 0) {
            savedTargets[arrow.direction] = arrow.targetIndex;
        }
    }

    m_arrows.clear();
    for (Direction dir : directions) {
        ArrowButton btn;
        btn.direction = dir;
        btn.targetIndex = savedTargets.value(dir, -1);
        m_arrows.append(btn);
    }
    recalculateHitRects();
    update();
}

void ArrowOverlay::setSourceIndex(int index)
{
    m_sourceIndex = index;
}

void ArrowOverlay::setTargetIndex(Direction dir, int targetIndex)
{
    for (auto &arrow : m_arrows) {
        if (arrow.direction == dir) {
            arrow.targetIndex = targetIndex;
            update();
            return;
        }
    }
    // Direction not yet in list — store for later when setDirections is called
    ArrowButton btn;
    btn.direction = dir;
    btn.targetIndex = targetIndex;
    m_arrows.append(btn);
}

int ArrowOverlay::targetIndex(Direction dir) const
{
    for (const auto &arrow : m_arrows) {
        if (arrow.direction == dir) {
            return arrow.targetIndex;
        }
    }
    return -1;
}

void ArrowOverlay::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    for (int i = 0; i < m_arrows.size(); ++i) {
        if (m_arrows[i].targetIndex >= 0) {
            bool isHovered = (i == m_hoveredArrowIdx);
            bool isPressed = (i == m_pressedArrowIdx);
            drawArrow(painter, m_arrows[i], isPressed, isHovered);
        }
    }
}

void ArrowOverlay::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    recalculateHitRects();
}

void ArrowOverlay::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        int idx = hitTest(event->pos());
        if (idx >= 0 && m_arrows[idx].targetIndex >= 0) {
            m_pressedArrowIdx = idx;
            m_isHolding = true;
            m_pressTimer.start();
            emit arrowPressed(m_sourceIndex, m_arrows[idx].targetIndex);
            update();
            return;
        }
    }
    // Not on an arrow — let the event pass through to the parent widget
    event->ignore();
}

void ArrowOverlay::mouseMoveEvent(QMouseEvent *event)
{
    int idx = hitTest(event->pos());
    if (idx != m_hoveredArrowIdx) {
        m_hoveredArrowIdx = idx;
        if (idx >= 0) {
            setCursor(Qt::PointingHandCursor);
        } else {
            unsetCursor();
        }
        update();
    }
    // If not holding an arrow, let the event pass through
    if (m_pressedArrowIdx < 0) {
        event->ignore();
    }
}

void ArrowOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_pressedArrowIdx >= 0) {
        int idx = m_pressedArrowIdx;
        m_pressedArrowIdx = -1;

        if (m_isHolding && idx < m_arrows.size()) {
            qint64 elapsed = m_pressTimer.elapsed();
            int releaseIdx = hitTest(event->pos());

            if (elapsed >= kHoldThresholdMs) {
                // Long press (hold) — just restore, no click
                emit arrowReleased(m_sourceIndex, m_arrows[idx].targetIndex);
            } else {
                // Short press (click) — undo the press preview, then toggle
                emit arrowReleased(m_sourceIndex, m_arrows[idx].targetIndex);
                if (releaseIdx == idx) {
                    emit arrowClicked(m_sourceIndex, m_arrows[idx].targetIndex);
                }
            }
        }
        m_isHolding = false;
        update();
        return;
    }
    // Not on an arrow — let the event pass through
    event->ignore();
}

void ArrowOverlay::wheelEvent(QWheelEvent *event)
{
    // Always pass wheel events through to parent (for zoom)
    event->ignore();
}

void ArrowOverlay::recalculateHitRects()
{
    QRect r = rect();
    if (r.isEmpty()) return; // Don't calculate with zero size

    int cx = r.width() / 2;
    int cy = r.height() / 2;

    for (auto &arrow : m_arrows) {
        switch (arrow.direction) {
        case Left:
            arrow.hitRect = QRect(kArrowMargin, cy - kArrowSize / 2,
                                  kArrowSize, kArrowSize);
            break;
        case Right:
            arrow.hitRect = QRect(r.width() - kArrowMargin - kArrowSize,
                                  cy - kArrowSize / 2, kArrowSize, kArrowSize);
            break;
        case Up:
            arrow.hitRect = QRect(cx - kArrowSize / 2, kArrowMargin,
                                  kArrowSize, kArrowSize);
            break;
        case Down:
            arrow.hitRect = QRect(cx - kArrowSize / 2,
                                  r.height() - kArrowMargin - kArrowSize,
                                  kArrowSize, kArrowSize);
            break;
        }
    }
}

int ArrowOverlay::hitTest(const QPoint &pos) const
{
    for (int i = 0; i < m_arrows.size(); ++i) {
        if (m_arrows[i].hitRect.contains(pos)) {
            return i;
        }
    }
    return -1;
}

void ArrowOverlay::drawArrow(QPainter &painter, const ArrowButton &arrow,
                              bool pressed, bool hovered) const
{
    QColor bgColor;
    QColor fgColor;
    QColor shadowColor(0, 0, 0, 20);

    if (pressed) {
        // Pressed: brand accent background, white arrow
        bgColor = QColor(0x00, 0x78, 0xD4, 230);  // brandPrimary
        fgColor = Qt::white;
        shadowColor = QColor(0, 0, 0, 40);
    } else if (hovered) {
        // Hover: brighter white, stronger shadow
        bgColor = QColor(255, 255, 255, 240);
        fgColor = QColor(0x1A, 0x1A, 0x1A);       // neutralForeground1
        shadowColor = QColor(0, 0, 0, 30);
    } else {
        // Normal: frosted glass white circle
        bgColor = QColor(255, 255, 255, 200);
        fgColor = QColor(0x61, 0x61, 0x61);        // neutralForeground2
    }

    // Draw shadow (offset 1px down)
    painter.setPen(Qt::NoPen);
    painter.setBrush(shadowColor);
    painter.drawEllipse(arrow.hitRect.adjusted(0, 1, 0, 1));

    // Draw circular background
    painter.setPen(QPen(QColor(0, 0, 0, 15), 1));  // subtle border
    painter.setBrush(bgColor);
    painter.drawEllipse(arrow.hitRect);

    // Draw chevron arrow (V-shape lines instead of solid triangle)
    drawChevron(painter, arrow.direction, arrow.hitRect, fgColor);
}

void ArrowOverlay::drawChevron(QPainter &painter, Direction dir,
                                const QRect &rect, const QColor &color) const
{
    qreal cx = rect.center().x();
    qreal cy = rect.center().y();
    qreal s = rect.width() * 0.22;  // chevron half-size

    QPen pen(color, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    QPainterPath path;
    switch (dir) {
    case Left:
        path.moveTo(cx + s * 0.4, cy - s);
        path.lineTo(cx - s * 0.6, cy);
        path.lineTo(cx + s * 0.4, cy + s);
        break;
    case Right:
        path.moveTo(cx - s * 0.4, cy - s);
        path.lineTo(cx + s * 0.6, cy);
        path.lineTo(cx - s * 0.4, cy + s);
        break;
    case Up:
        path.moveTo(cx - s, cy + s * 0.4);
        path.lineTo(cx, cy - s * 0.6);
        path.lineTo(cx + s, cy + s * 0.4);
        break;
    case Down:
        path.moveTo(cx - s, cy - s * 0.4);
        path.lineTo(cx, cy + s * 0.6);
        path.lineTo(cx + s, cy - s * 0.4);
        break;
    }
    painter.drawPath(path);
}
