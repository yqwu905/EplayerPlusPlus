#ifndef BROWSEPANEL_H
#define BROWSEPANEL_H

#include <QWidget>
#include <QList>

class QHBoxLayout;
class QScrollArea;
class CompareSession;
class ImageListModel;
class ImageLoader;
class ThumbnailWidget;

/**
 * @brief Image browsing panel with multi-column thumbnail layout.
 *
 * Each folder in the CompareSession is displayed as a vertically scrollable
 * column of thumbnails. Supports three selection modes:
 *   - Click: select single image in its column only (other columns unchanged)
 *   - Ctrl+Click: select image + same-filename images in other columns
 *   - Alt+Click: select image + same-index (order) images in other columns
 */
class BrowsePanel : public QWidget
{
    Q_OBJECT

public:
    explicit BrowsePanel(CompareSession *session, ImageLoader *imageLoader,
                         QWidget *parent = nullptr);
    ~BrowsePanel() override;

signals:
    /**
     * @brief Emitted when the image selection changes.
     *
     * Each inner list contains {folderPath, imagePath} pairs for selected images.
     */
    void selectionChanged(const QList<QPair<QString, QString>> &selectedImages);

private slots:
    void onFolderAdded(const QString &folderPath, int index);
    void onFolderRemoved(const QString &folderPath, int index);
    void onSessionCleared();
    void onThumbnailClicked(const QString &filePath, Qt::KeyboardModifiers modifiers);

private:
    struct ColumnInfo {
        QScrollArea *scrollArea = nullptr;
        QWidget *container = nullptr;
        ImageListModel *model = nullptr;
        QList<ThumbnailWidget *> thumbnailWidgets;
    };

    void setupUi();
    void rebuildColumn(int columnIndex);
    void clearAllColumns();
    void clearSelection();
    void clearColumnSelection(int column);
    void emitSelectionChanged();

    // Find which column and index a file path belongs to
    int findColumn(const QString &filePath) const;
    int findIndexInColumn(int column, const QString &filePath) const;

    CompareSession *m_session = nullptr;
    ImageLoader *m_imageLoader = nullptr;
    QHBoxLayout *m_columnsLayout = nullptr;
    QList<ColumnInfo> m_columns;
};

#endif // BROWSEPANEL_H
