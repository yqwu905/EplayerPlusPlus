#ifndef ZOOMABLEIMAGEWIDGET_H
#define ZOOMABLEIMAGEWIDGET_H

#include <QWidget>
#include <QImage>
#include <QPixmap>
#include <QPointF>

/**
 * @brief A widget that displays an image with zoom and pan support.
 *
 * Replaces QLabel for image display in the ComparePanel. Supports:
 *   - Mouse wheel zoom (centered on cursor position)
 *   - Left-button drag to pan
 *   - Double-click to reset view (fit-to-view)
 *
 * Emits signals on zoom/pan/reset so ComparePanel can synchronize
 * the view across multiple images (linked mode).
 */
class ZoomableImageWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ZoomableImageWidget(QWidget *parent = nullptr);
    ~ZoomableImageWidget() override;

    /**
     * @brief Set the image to display.
     */
    void setImage(const QImage &image, bool resetView = true);
    void setImage(const QPixmap &pixmap, bool resetView = true);

    /**
     * @brief Set placeholder text when no image is loaded.
     */
    void setText(const QString &text);

    /**
     * @brief Get the current zoom level. 1.0 = fit-to-view.
     */
    double zoomLevel() const { return m_zoomLevel; }

    /**
     * @brief Get the current pan offset in image pixels.
     *
     * This is the raw internal pan offset (image pixels). It is dependent on
     * the image's dimensions and therefore not directly suitable for syncing
     * across cells that display images of different sizes. For cross-cell
     * synchronization, use normalizedPan() / setNormalizedPan() instead.
     */
    QPointF panOffset() const { return m_panOffset; }

    /**
     * @brief Get the current pan offset in normalized image coordinates.
     *
     * Returns (panOffsetPixels.x / image.width(), panOffsetPixels.y /
     * image.height()). Returns (0,0) when no image is loaded. Because the
     * value is image-size independent, this is the right unit for syncing
     * pan between cells whose images may have different dimensions.
     */
    QPointF normalizedPan() const;

    /**
     * @brief Get the current image.
     */
    QImage image() const { return m_image; }

    /**
     * @brief Check whether an image is loaded.
     */
    bool hasImage() const { return !m_image.isNull(); }

    /**
     * @brief Programmatically set zoom level (used for linked view sync).
     *
     * The pan offset is adjusted so the image pixel at @p focalPoint stays at
     * the same widget-space position across the zoom change (focal-point
     * anchoring). This matches the behaviour of mouse-wheel zoom and ensures
     * that linked cells zoom about the same image-relative anchor.
     *
     * @param level Zoom level (1.0 = fit-to-view).
     * @param focalPoint Focal point in normalized image coordinates [0,1].
     * @param emitSignal If false, don't emit zoomChanged (to avoid loops).
     */
    void setZoomLevel(double level, QPointF focalPoint, bool emitSignal = false);

    /**
     * @brief Programmatically set pan offset in normalized image coordinates.
     *
     * The normalized value is converted to image pixels for the currently
     * loaded image. This makes cross-cell pan sync correct even when cells
     * display images of different dimensions.
     *
     * @param normalizedOffset Pan offset, image-size independent. Each
     *        component is in (panOffsetPixels / imageDimension) units.
     * @param emitSignal If false, don't emit panChanged (to avoid loops).
     */
    void setNormalizedPan(QPointF normalizedOffset, bool emitSignal = false);

    /**
     * @brief Reset to fit-to-view.
     * @param emitSignal If false, don't emit viewReset (to avoid loops).
     */
    void resetView(bool emitSignal = false);

signals:
    /**
     * @brief Emitted when zoom level changes via user interaction.
     * @param zoomLevel New zoom level.
     * @param focalPoint Focal point in normalized image coordinates [0,1].
     */
    void zoomChanged(double zoomLevel, QPointF focalPoint);

    /**
     * @brief Emitted when pan offset changes via user interaction.
     * @param normalizedOffset New pan offset in normalized image coordinates
     *        (panOffsetPixels / imageDimension). Image-size independent so the
     *        value can be applied verbatim to linked cells.
     */
    void panChanged(QPointF normalizedOffset);

    /**
     * @brief Emitted when the view is reset via user interaction.
     */
    void viewReset();

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    /**
     * @brief Compute the base scale factor that fits the image into the widget.
     */
    double fitScale() const;

    /**
     * @brief Compute the effective scale: fitScale * zoomLevel.
     */
    double effectiveScale() const;

    /**
     * @brief Map a widget coordinate to normalized image coordinate [0,1].
     */
    QPointF widgetToNormalized(const QPointF &widgetPos) const;

    /**
     * @brief Clamp pan offset to keep the image within reasonable bounds.
     */
    void clampPanOffset();

    /**
     * @brief Apply a new zoom level while anchoring an image point under a
     *        fixed widget-space position.
     *
     * Shared math for mouse-wheel zoom and programmatic setZoomLevel. The
     * image pixel currently displayed at @p anchorWidget remains at that
     * widget position after the zoom by adjusting m_panOffset accordingly.
     * The new level is clamped to [kMinZoom, kMaxZoom]. If the resulting
     * zoom is 1.0, pan is reset to zero (so fit-to-view stays fitted).
     */
    void applyZoom(double newLevel, const QPointF &anchorWidget);

    /**
     * @brief Convert a normalized image coordinate [0,1]^2 to a widget-space
     *        position using the current zoom and pan.
     */
    QPointF normalizedImageToWidget(const QPointF &normalizedImage) const;

    QImage m_image;
    QString m_text;           ///< Placeholder text when no image
    double m_zoomLevel = 1.0; ///< 1.0 = fit-to-view, >1.0 = zoomed in
    QPointF m_panOffset;      ///< Pan offset in image pixels

    bool m_isPanning = false;
    QPoint m_lastMousePos;

    static constexpr double kMinZoom = 1.0;
    static constexpr double kMaxZoom = 50.0;
    static constexpr double kZoomStep = 1.15; // 15% per wheel step
};

#endif // ZOOMABLEIMAGEWIDGET_H
