#include "ArrowOverlay.h"

#include <QPainter>
#include <QMouseEvent>
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
            drawArrow(painter, m_arrows[i], (i == m_pressedArrowIdx));
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
    QWidget::mousePressEvent(event);
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
    QWidget::mouseReleaseEvent(event);
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
                              bool pressed) const
{
    QColor bgColor = pressed ? QColor(0, 120, 215, 200) : QColor(0, 0, 0, 120);
    QColor fgColor = Qt::white;

    // Draw circular background
    painter.setPen(Qt::NoPen);
    painter.setBrush(bgColor);
    painter.drawEllipse(arrow.hitRect);

    // Draw arrow polygon
    QPolygonF poly = arrowPolygon(arrow.direction, arrow.hitRect);
    painter.setPen(Qt::NoPen);
    painter.setBrush(fgColor);
    painter.drawPolygon(poly);
}

QPolygonF ArrowOverlay::arrowPolygon(Direction dir, const QRect &rect) const
{
    QPolygonF poly;
    qreal cx = rect.center().x();
    qreal cy = rect.center().y();
    qreal s = rect.width() * 0.3; // arrow size relative to hitRect

    switch (dir) {
    case Left:
        poly << QPointF(cx - s, cy)
             << QPointF(cx + s * 0.5, cy - s)
             << QPointF(cx + s * 0.5, cy + s);
        break;
    case Right:
        poly << QPointF(cx + s, cy)
             << QPointF(cx - s * 0.5, cy - s)
             << QPointF(cx - s * 0.5, cy + s);
        break;
    case Up:
        poly << QPointF(cx, cy - s)
             << QPointF(cx - s, cy + s * 0.5)
             << QPointF(cx + s, cy + s * 0.5);
        break;
    case Down:
        poly << QPointF(cx, cy + s)
             << QPointF(cx - s, cy - s * 0.5)
             << QPointF(cx + s, cy - s * 0.5);
        break;
    }

    return poly;
}
