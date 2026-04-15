#ifndef ARROWOVERLAY_H
#define ARROWOVERLAY_H

#include <QWidget>
#include <QList>
#include <QRect>
#include <QElapsedTimer>

/**
 * @brief Overlay widget that draws directional arrows between comparison images.
 *
 * Placed on top of an image in the ComparePanel. Draws arrow buttons pointing
 * toward other images. Supports two interaction modes:
 *   - Press and hold: temporarily replace target image with source image
 *   - Click: toggle between target original and tolerance map
 */
class ArrowOverlay : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief Direction of an arrow pointing to another image.
     */
    enum Direction {
        Left,
        Right,
        Up,
        Down
    };

    explicit ArrowOverlay(QWidget *parent = nullptr);
    ~ArrowOverlay() override;

    /**
     * @brief Set which arrow directions are active / visible.
     */
    void setDirections(const QList<Direction> &directions);

    /**
     * @brief Set the source image index (the image this overlay belongs to).
     */
    void setSourceIndex(int index);
    int sourceIndex() const { return m_sourceIndex; }

    /**
     * @brief Map a direction to the target image index.
     */
    void setTargetIndex(Direction dir, int targetIndex);
    int targetIndex(Direction dir) const;

signals:
    /**
     * @brief Arrow button pressed (held down).
     * @param sourceIndex Index of the source image (overlay owner).
     * @param targetIndex Index of the target image (arrow destination).
     */
    void arrowPressed(int sourceIndex, int targetIndex);

    /**
     * @brief Arrow button released (after being held).
     * @param sourceIndex Index of the source image.
     * @param targetIndex Index of the target image.
     */
    void arrowReleased(int sourceIndex, int targetIndex);

    /**
     * @brief Arrow button clicked (click-and-release, not held).
     * @param sourceIndex Index of the source image.
     * @param targetIndex Index of the target image.
     */
    void arrowClicked(int sourceIndex, int targetIndex);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    struct ArrowButton {
        Direction direction;
        int targetIndex = -1;
        QRect hitRect;
    };

    QList<ArrowButton> m_arrows;
    int m_sourceIndex = -1;
    int m_pressedArrowIdx = -1;
    int m_hoveredArrowIdx = -1;
    bool m_isHolding = false;
    QElapsedTimer m_pressTimer;

    static constexpr int kArrowSize = 36;
    static constexpr int kArrowMargin = 10;
    static constexpr int kHoldThresholdMs = 300;

    void recalculateHitRects();
    int hitTest(const QPoint &pos) const;
    void drawArrow(QPainter &painter, const ArrowButton &arrow,
                   bool pressed, bool hovered) const;
    void drawChevron(QPainter &painter, Direction dir,
                     const QRect &rect, const QColor &color) const;
};

#endif // ARROWOVERLAY_H
