#ifndef THUMBNAILWIDGET_H
#define THUMBNAILWIDGET_H

#include <QWidget>
#include <QImage>
#include <QPixmap>

/**
 * @brief Widget displaying a single image thumbnail with filename.
 *
 * Shows a scaled thumbnail image with the filename below it.
 * Supports selected state visual feedback.
 */
class ThumbnailWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ThumbnailWidget(QWidget *parent = nullptr);
    ~ThumbnailWidget() override;

    /**
     * @brief Set the thumbnail image to display.
     */
    void setThumbnail(const QImage &thumbnail);

    /**
     * @brief Set the filename to display below the thumbnail.
     */
    void setFileName(const QString &fileName);

    /**
     * @brief Set the image file path (stored for identification).
     */
    void setFilePath(const QString &filePath);

    /**
     * @brief Get the image file path.
     */
    QString filePath() const { return m_filePath; }

    /**
     * @brief Get the filename.
     */
    QString fileName() const { return m_fileName; }

    /**
     * @brief Set the selected state.
     */
    void setSelected(bool selected);

    /**
     * @brief Check if the widget is selected.
     */
    bool isSelected() const { return m_selected; }

    /**
     * @brief Set the thumbnail display size.
     */
    void setThumbnailSize(const QSize &size);

    QSize sizeHint() const override;

signals:
    /**
     * @brief Emitted when the thumbnail is clicked.
     * @param filePath The file path of the clicked image.
     * @param modifiers Keyboard modifiers active during the click.
     */
    void clicked(const QString &filePath, Qt::KeyboardModifiers modifiers);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QPixmap m_pixmap;
    QString m_fileName;
    QString m_filePath;
    bool m_selected = false;
    bool m_hovered = false;
    QSize m_thumbnailSize = QSize(180, 180);
    static constexpr int kTextHeight = 24;
    static constexpr int kPadding = 4;
    static constexpr int kSelectionBorder = 3;
};

#endif // THUMBNAILWIDGET_H
