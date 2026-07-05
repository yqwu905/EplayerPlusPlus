#include "BrowsePanel.h"
#include "FlowLayout.h"
#include "ImageContextMenu.h"
#include "models/CompareSession.h"
#include "models/ImageListModel.h"
#include "services/ImageLoader.h"
#include "services/ImageMarkManager.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDataStream>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIODevice>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMargins>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollBar>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <limits>
#include <vector>

namespace
{
// Baseline thumbnail geometry. These are now the *minimum* / aspect-ratio basis:
// the live size scales up from here as the browse column is widened.
constexpr int kThumbnailCardWidth = 166;
constexpr int kThumbnailImageWidth = 154;
constexpr int kThumbnailImageHeight = 96;
constexpr int kThumbnailItemHeight = 142;
// Fixed chrome around the scalable image area, derived from the baseline so the
// card padding and the (non-scaling) filename row keep their original sizes.
constexpr int kHorizontalChrome = kThumbnailCardWidth - kThumbnailImageWidth;   // 12
constexpr int kVerticalChrome = kThumbnailItemHeight - kThumbnailImageHeight;   // 46
// Bounds for the on-screen image width as the column is resized. The minimum is
// deliberately small so several columns can be squeezed into a narrow panel (each
// column = panelWidth / N); the user enlarges thumbnails by dragging the panel
// wider. The minimum must stay below the header's intrinsic floor (~58px) so the
// thumbnail — not a fixed minimum — is what limits how narrow a column can get.
constexpr int kMinImageWidth = 40;
constexpr int kMaxImageWidth = 460;
// Small inset so the card never collides with the vertical scrollbar.
constexpr int kCardBreath = 4;
// Decode "buckets": thumbnails are decoded at a quantized size and painted scaled
// to the exact rect. Quantizing keeps the number of distinct cached sizes (and the
// re-decode rate during a drag) small; the cap bounds per-image memory.
constexpr int kDecodeBucketStep = 64;
constexpr int kMinDecodeBucket = 128;
constexpr int kMaxDecodeBucket = 320;
constexpr int kScrollAreaSafetyPadding = 4;
// Filter toolbar control widths; also used to floor the panel width so the two
// inputs always fit on a single row even when only one folder column is shown.
constexpr int kFilterEditWidth = 120;
constexpr int kFilterComboWidth = 110;
constexpr int kFilterRowSpacing = 8;
constexpr int kPrefetchMinimumRows = 32;
constexpr int kMarkButtonSize = 18;
constexpr int kMarkButtonGap = 3;
constexpr int kMarkButtonTopMargin = 10;
constexpr int kMarkButtonRightMargin = 10;
constexpr int kMaxAutoFitColumns = 6;
const char kFolderOrderMimeType[] = "application/x-eplayer-folder-index";
const QString kUnmarkedCategoryFilter = QStringLiteral("__unmarked__");

// Defined on BrowsePanel so it can be a member; the delegate, the list view and
// the free helpers below all share it (by pointer) so paint and hit-testing can
// never disagree about where the card, image and mark buttons are.
using ThumbMetrics = BrowsePanel::ThumbMetrics;

// Build metrics for a target on-screen image width, preserving the image aspect
// ratio and snapping the decode size to a bucket.
ThumbMetrics metricsForImageWidth(int targetImageWidth)
{
    ThumbMetrics m;
    m.imageWidth = qBound(kMinImageWidth, targetImageWidth, kMaxImageWidth);
    m.imageHeight = qRound(double(m.imageWidth) * kThumbnailImageHeight / kThumbnailImageWidth);
    m.cardWidth = m.imageWidth + kHorizontalChrome;
    m.itemHeight = m.imageHeight + kVerticalChrome;
    const int longSide = qMax(m.imageWidth, m.imageHeight);
    const int bucket = ((longSide + kDecodeBucketStep - 1) / kDecodeBucketStep) * kDecodeBucketStep;
    m.decodeExtent = qBound(kMinDecodeBucket, bucket, kMaxDecodeBucket);
    return m;
}

const QColor kBrowseColors[] = {
    QColor(0x18, 0x6F, 0xD7),
    QColor(0x22, 0x8A, 0x46),
    QColor(0xF7, 0x73, 0x13),
    QColor(0x74, 0x55, 0xC8),
    QColor(0x0F, 0x7B, 0x93),
    QColor(0xC5, 0x0F, 0x1F)
};
const int kBrowseColorCount = sizeof(kBrowseColors) / sizeof(kBrowseColors[0]);

QColor browseColor(int index)
{
    return kBrowseColors[qMax(0, index) % kBrowseColorCount];
}

int primarySelectedIndex(const ImageListModel *model)
{
    if (!model) {
        return -1;
    }

    QList<int> selected = model->selectedIndices();
    if (selected.isEmpty()) {
        return -1;
    }

    std::sort(selected.begin(), selected.end());
    return selected.first();
}

QByteArray encodeFolderOrderDragIndex(int index)
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream << index;
    return payload;
}

bool decodeFolderOrderDragIndex(const QMimeData *mimeData, int *index)
{
    if (!mimeData || !index ||
        !mimeData->hasFormat(QString::fromLatin1(kFolderOrderMimeType))) {
        return false;
    }

    QByteArray payload = mimeData->data(QString::fromLatin1(kFolderOrderMimeType));
    QDataStream stream(&payload, QIODevice::ReadOnly);
    stream >> *index;
    return stream.status() == QDataStream::Ok;
}

QRect thumbnailCardRect(const QRect &itemRect, const ThumbMetrics &m)
{
    const int cardX = itemRect.x() + qMax(0, (itemRect.width() - m.cardWidth) / 2);
    return QRect(cardX, itemRect.y() + 2, m.cardWidth, m.itemHeight - 4);
}

// The actual thumbnail picture area within an item — matches the rect the
// delegate paints the image into. Used to distinguish a right-click "on the
// image" from one on the surrounding non-image area.
QRect thumbnailImageRect(const QRect &itemRect, const ThumbMetrics &m)
{
    const QRect cardRect = thumbnailCardRect(itemRect, m);
    return QRect(cardRect.x() + 6, cardRect.y() + 6,
                 m.imageWidth, m.imageHeight);
}

QRect markButtonRect(const QRect &itemRect, int categoryIndex, const ThumbMetrics &m)
{
    const QRect cardRect = thumbnailCardRect(itemRect, m);
    const int categoryCount = ImageMarkManager::categories().size();
    const int totalWidth = categoryCount * kMarkButtonSize
                           + (categoryCount - 1) * kMarkButtonGap;
    const int x = cardRect.x() + cardRect.width() - kMarkButtonRightMargin
                  - totalWidth + categoryIndex * (kMarkButtonSize + kMarkButtonGap);
    const int y = cardRect.y() + kMarkButtonTopMargin;
    return QRect(x, y, kMarkButtonSize, kMarkButtonSize);
}

QString markCategoryAtPosition(const QRect &itemRect, const QPoint &pos, const ThumbMetrics &m)
{
    const QStringList categories = ImageMarkManager::categories();
    for (int i = 0; i < categories.size(); ++i) {
        if (markButtonRect(itemRect, i, m).contains(pos)) {
            return categories.at(i);
        }
    }
    return QString();
}

void paintMarkButtons(QPainter *painter,
                      const QRect &itemRect,
                      const QString &currentMark,
                      bool currentMarkByVlm,
                      bool hovered,
                      const ThumbMetrics &m)
{
    if (currentMark.isEmpty() && !hovered) {
        return;
    }

    const QStringList categories = ImageMarkManager::categories();
    QFont buttonFont = painter->font();
    buttonFont.setPointSize(8);
    buttonFont.setWeight(QFont::DemiBold);
    painter->setFont(buttonFont);

    for (int i = 0; i < categories.size(); ++i) {
        const QString category = categories.at(i);
        const QRect rect = markButtonRect(itemRect, i, m);
        const bool active = (category == currentMark);
        const bool aiActive = active && currentMarkByVlm;

        painter->setPen(QPen(aiActive ? QColor(0x0F, 0x7B, 0x93)
                                      : (active ? QColor(0x00, 0x78, 0xD4)
                                                : QColor(0xC8, 0xC8, 0xC8)),
                             aiActive ? 2 : 1));
        painter->setBrush(aiActive ? QColor(0xE6, 0xF6, 0xFA)
                                   : (active ? QColor(0x00, 0x78, 0xD4)
                                             : QColor(255, 255, 255, 230)));
        painter->drawRoundedRect(rect, 4, 4);
        painter->setPen(aiActive ? QColor(0x0B, 0x5E, 0x70)
                                 : (active ? Qt::white : QColor(0x42, 0x42, 0x42)));
        painter->drawText(rect, Qt::AlignCenter, aiActive ? category + QStringLiteral("*")
                                                          : category);
    }
}

class ThumbnailListView final : public QListView
{
public:
    using MarkCallback = std::function<void(const QModelIndex &,
                                            const QString &,
                                            Qt::KeyboardModifiers)>;
    using ContextMenuCallback = std::function<void(const QModelIndex &, const QPoint &)>;
    using BlankContextMenuCallback = std::function<void(const QPoint &)>;
    using ColumnIndexCallback = std::function<int()>;
    using SwapCallback = std::function<void(int, int)>;

    explicit ThumbnailListView(QWidget *parent = nullptr)
        : QListView(parent)
    {
        setAcceptDrops(true);
    }

    void setMarkCallback(MarkCallback callback)
    {
        m_markCallback = std::move(callback);
    }

    void setContextMenuCallback(ContextMenuCallback callback)
    {
        m_contextMenuCallback = std::move(callback);
    }

    void setBlankContextMenuCallback(BlankContextMenuCallback callback)
    {
        m_blankContextMenuCallback = std::move(callback);
    }

    void setColumnIndexCallback(ColumnIndexCallback callback)
    {
        m_columnIndexCallback = std::move(callback);
    }

    void setSwapCallback(SwapCallback callback)
    {
        m_swapCallback = std::move(callback);
    }

    // Live thumbnail geometry, owned by BrowsePanel; used so hit-testing matches
    // exactly what the delegate paints.
    void setMetrics(const ThumbMetrics *metrics)
    {
        m_metrics = metrics;
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            const QModelIndex modelIndex = indexAt(event->pos());
            if (modelIndex.isValid()) {
                const QString category = markCategoryAtPosition(visualRect(modelIndex),
                                                                event->pos(), metrics());
                if (!category.isEmpty()) {
                    m_dragCandidateIndex = QModelIndex();
                    if (m_markCallback) {
                        m_markCallback(modelIndex, category, event->modifiers());
                    }
                    event->accept();
                    return;
                }

                m_dragStartPos = event->pos();
                m_dragCandidateIndex = modelIndex;
            } else {
                m_dragCandidateIndex = QModelIndex();
            }
        }

        QListView::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if ((event->buttons() & Qt::LeftButton) &&
            m_dragCandidateIndex.isValid() &&
            (event->pos() - m_dragStartPos).manhattanLength() >= QApplication::startDragDistance()) {
            startFolderDrag();
            event->accept();
            return;
        }

        QListView::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_dragCandidateIndex = QModelIndex();
        }
        QListView::mouseReleaseEvent(event);
    }

    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (canAcceptFolderDrop(event->mimeData())) {
            event->acceptProposedAction();
            return;
        }
        QListView::dragEnterEvent(event);
    }

    void dragMoveEvent(QDragMoveEvent *event) override
    {
        if (canAcceptFolderDrop(event->mimeData())) {
            event->acceptProposedAction();
            return;
        }
        QListView::dragMoveEvent(event);
    }

    void dropEvent(QDropEvent *event) override
    {
        int sourceIndex = -1;
        if (!decodeFolderOrderDragIndex(event->mimeData(), &sourceIndex) ||
            !m_columnIndexCallback || !m_swapCallback) {
            QListView::dropEvent(event);
            return;
        }

        const int targetIndex = m_columnIndexCallback();
        if (sourceIndex >= 0 && targetIndex >= 0 && sourceIndex != targetIndex) {
            m_swapCallback(sourceIndex, targetIndex);
            event->acceptProposedAction();
            return;
        }

        QListView::dropEvent(event);
    }

    void contextMenuEvent(QContextMenuEvent *event) override
    {
        if (showContextMenu(event)) {
            return;
        }

        QListView::contextMenuEvent(event);
    }

    bool viewportEvent(QEvent *event) override
    {
        if (event->type() == QEvent::ContextMenu &&
            showContextMenu(static_cast<QContextMenuEvent *>(event))) {
            return true;
        }

        return QListView::viewportEvent(event);
    }

private:
    bool canAcceptFolderDrop(const QMimeData *mimeData) const
    {
        int sourceIndex = -1;
        if (!decodeFolderOrderDragIndex(mimeData, &sourceIndex) || !m_columnIndexCallback) {
            return false;
        }

        const int targetIndex = m_columnIndexCallback();
        return sourceIndex >= 0 && targetIndex >= 0 && sourceIndex != targetIndex;
    }

    void startFolderDrag()
    {
        if (!m_columnIndexCallback) {
            return;
        }

        const int sourceIndex = m_columnIndexCallback();
        if (sourceIndex < 0) {
            return;
        }

        auto *mimeData = new QMimeData();
        mimeData->setData(QString::fromLatin1(kFolderOrderMimeType),
                          encodeFolderOrderDragIndex(sourceIndex));

        auto *drag = new QDrag(this);
        drag->setMimeData(mimeData);

        const QRect rect = visualRect(m_dragCandidateIndex);
        if (rect.isValid()) {
            QPixmap pixmap = viewport()->grab(rect);
            if (!pixmap.isNull()) {
                drag->setPixmap(pixmap);
                drag->setHotSpot(QPoint(pixmap.width() / 2, pixmap.height() / 2));
            }
        }

        m_dragCandidateIndex = QModelIndex();
        drag->exec(Qt::MoveAction);
    }

    bool showContextMenu(QContextMenuEvent *event)
    {
        const QModelIndex modelIndex = indexAt(event->pos());
        // A right-click counts as "on the image" only when it lands on the
        // actual thumbnail picture; everywhere else in the column (card padding,
        // text, gaps, empty space) is treated as non-image → folder menu.
        const bool onImage = modelIndex.isValid()
            && thumbnailImageRect(visualRect(modelIndex), metrics()).contains(event->pos());

        if (onImage && m_contextMenuCallback) {
            m_contextMenuCallback(modelIndex, event->globalPos());
            event->accept();
            return true;
        }
        if (m_blankContextMenuCallback) {
            m_blankContextMenuCallback(event->globalPos());
            event->accept();
            return true;
        }
        return false;
    }

    // Returns the live metrics, or sensible defaults if not yet wired.
    ThumbMetrics metrics() const
    {
        return m_metrics ? *m_metrics : ThumbMetrics{};
    }

    MarkCallback m_markCallback;
    ContextMenuCallback m_contextMenuCallback;
    BlankContextMenuCallback m_blankContextMenuCallback;
    ColumnIndexCallback m_columnIndexCallback;
    SwapCallback m_swapCallback;
    const ThumbMetrics *m_metrics = nullptr;
    QPoint m_dragStartPos;
    QModelIndex m_dragCandidateIndex;
};

class ThumbnailDelegate final : public QStyledItemDelegate
{
public:
    explicit ThumbnailDelegate(int colorIndex, const ThumbMetrics *metrics, QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
        , m_accentColor(browseColor(colorIndex))
        , m_metrics(metrics)
    {
    }

    QSize sizeHint(const QStyleOptionViewItem & /*option*/,
                   const QModelIndex & /*index*/) const override
    {
        const ThumbMetrics m = metrics();
        return QSize(m.cardWidth + 16, m.itemHeight);
    }

    void paint(QPainter *painter,
               const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        const ThumbMetrics m = metrics();
        const bool selected = index.data(ImageListModel::IsSelectedRole).toBool();
        const QString currentMark = index.data(ImageListModel::MarkRole).toString();
        const bool currentMarkByVlm =
            index.data(ImageListModel::MarkSourceRole).toString() == ImageMarkManager::vlmSource();
        const bool hovered = option.state & QStyle::State_MouseOver;
        const QRect cardRect = thumbnailCardRect(option.rect, m);

        if (selected) {
            painter->setPen(QPen(m_accentColor, 2));
            painter->setBrush(QColor(0xFF, 0xFF, 0xFF));
        } else if (hovered) {
            painter->setPen(QPen(QColor(0xD1, 0xD1, 0xD1), 1));
            painter->setBrush(QColor(0xFF, 0xFF, 0xFF));
        } else {
            painter->setPen(QPen(QColor(0xE0, 0xE0, 0xE0), 1));
            painter->setBrush(QColor(0xFF, 0xFF, 0xFF));
        }
        painter->drawRoundedRect(cardRect.adjusted(1, 1, -1, -1), 8, 8);

        const QRect thumbArea(cardRect.x() + 6,
                              cardRect.y() + 6,
                              m.imageWidth,
                              m.imageHeight);

        QPainterPath clipPath;
        clipPath.addRoundedRect(thumbArea, 6, 6);
        painter->setClipPath(clipPath);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0xF5, 0xF5, 0xF5));
        painter->drawRect(thumbArea);

        const QVariant thumbVar = index.data(ImageListModel::ThumbnailRole);
        const QImage thumbnail = thumbVar.canConvert<QImage>() ? thumbVar.value<QImage>() : QImage();
        if (!thumbnail.isNull()) {
            const QImage cover = coverFor(thumbnail, thumbArea.size());
            const int x = thumbArea.x() + (thumbArea.width() - cover.width()) / 2;
            const int y = thumbArea.y() + (thumbArea.height() - cover.height()) / 2;
            painter->drawImage(QPoint(x, y), cover);
        } else {
            painter->setPen(QColor(0x9E, 0x9E, 0x9E));
            QFont placeholderFont = painter->font();
            placeholderFont.setPointSize(10);
            painter->setFont(placeholderFont);
            painter->drawText(thumbArea, Qt::AlignCenter, QObject::tr("Loading..."));
        }
        painter->setClipping(false);

        if (selected) {
            const QRect checkRect(cardRect.right() - 25, cardRect.top() + 7, 18, 18);
            painter->setPen(Qt::NoPen);
            painter->setBrush(m_accentColor);
            painter->drawEllipse(checkRect);
            painter->setPen(QPen(Qt::white, 2));
            painter->drawLine(QPointF(checkRect.left() + 5, checkRect.center().y()),
                              QPointF(checkRect.left() + 8, checkRect.bottom() - 5));
            painter->drawLine(QPointF(checkRect.left() + 8, checkRect.bottom() - 5),
                              QPointF(checkRect.right() - 4, checkRect.top() + 5));
        }

        const QRect textArea(cardRect.x() + 7,
                             thumbArea.bottom() + 4,
                             m.imageWidth,
                             26);
        painter->setPen(QColor(0x61, 0x61, 0x61));
        QFont font = painter->font();
        font.setPointSize(10);
        painter->setFont(font);
        QFontMetrics fm(font);
        const QString fileName = index.data(ImageListModel::FileNameRole).toString();
        painter->drawText(textArea,
                          Qt::AlignCenter,
                          fm.elidedText(fileName, Qt::ElideMiddle, textArea.width()));

        paintMarkButtons(painter, option.rect, currentMark, currentMarkByVlm, hovered, m);

        painter->restore();
    }

private:
    ThumbMetrics metrics() const
    {
        return m_metrics ? *m_metrics : ThumbMetrics{};
    }

    // A smooth (bilinear) full rescale of every visible thumbnail ran on every
    // paint event — and ScrollPerPixel + hover repaint each card many times per
    // gesture across multiple columns. Cache the scaled cover keyed by the
    // source thumbnail's identity (QImage::cacheKey() changes when the model
    // swaps in an upgraded decode) so repeated paints at the same geometry reuse
    // it. The painted pixels are identical; only the redundant resamples go away.
    QImage coverFor(const QImage &src, const QSize &target) const
    {
        if (target != m_coverTargetSize) {
            // Every visible item shares one target size; a column resize changes
            // it for all of them, so the whole cache is stale at once.
            m_coverCache.clear();
            m_coverLru.clear();
            m_coverTargetSize = target;
        }
        const qint64 key = src.cacheKey();
        auto it = m_coverCache.find(key);
        if (it != m_coverCache.end()) {
            m_coverLru.removeOne(key);
            m_coverLru.prepend(key);
            return it.value();
        }
        QImage cover = src.scaled(target, Qt::KeepAspectRatioByExpanding,
                                  Qt::SmoothTransformation);
        m_coverCache.insert(key, cover);
        m_coverLru.prepend(key);
        while (m_coverLru.size() > kCoverCacheCap) {
            m_coverCache.remove(m_coverLru.takeLast());
        }
        return cover;
    }

    static constexpr int kCoverCacheCap = 96;

    QColor m_accentColor;
    const ThumbMetrics *m_metrics = nullptr;
    mutable QHash<qint64, QImage> m_coverCache;
    mutable QList<qint64> m_coverLru; // front = most-recently used
    mutable QSize m_coverTargetSize;
};

int rowExtent(const QListView *view, const ThumbMetrics &m)
{
    return m.itemHeight + (view ? view->spacing() : 0);
}
}

BrowsePanel::BrowsePanel(CompareSession *session, ImageLoader *imageLoader,
                         QWidget *parent)
    : QWidget(parent)
    , m_session(session)
    , m_imageLoader(imageLoader)
{
    setupUi();

    // Coalesces a burst of resize events (a splitter drag) into a single
    // re-decode once the size has settled, so dragging only rescales the cached
    // pixmaps live and never floods the decoder.
    m_decodeReloadTimer = new QTimer(this);
    m_decodeReloadTimer->setSingleShot(true);
    m_decodeReloadTimer->setInterval(150);
    connect(m_decodeReloadTimer, &QTimer::timeout,
            this, &BrowsePanel::onDecodeReloadTimeout);

    connect(m_session, &CompareSession::folderAdded,
            this, &BrowsePanel::onFolderAdded);
    connect(m_session, &CompareSession::folderRemoved,
            this, &BrowsePanel::onFolderRemoved);
    connect(m_session, &CompareSession::foldersSwapped,
            this, &BrowsePanel::onFoldersSwapped);
    connect(m_session, &CompareSession::cleared,
            this, &BrowsePanel::onSessionCleared);
}

BrowsePanel::~BrowsePanel()
{
    stopInterleavedLoading();
}

void BrowsePanel::setImageMarkManager(ImageMarkManager *manager)
{
    m_markManager = manager;
    for (auto &column : m_columns) {
        if (column.model) {
            column.model->setImageMarkManager(m_markManager);
        }
    }
}

void BrowsePanel::setFuzzyFileNameMatchEnabled(bool enabled)
{
    m_fuzzyFileNameMatch = enabled;
}

QList<VlmAnnotationService::ColumnSnapshot> BrowsePanel::currentColumnSnapshots() const
{
    QList<VlmAnnotationService::ColumnSnapshot> snapshots;
    snapshots.reserve(m_columns.size());

    for (int columnIndex = 0; columnIndex < m_columns.size(); ++columnIndex) {
        const ColumnInfo &column = m_columns.at(columnIndex);
        if (!column.model) {
            continue;
        }

        VlmAnnotationService::ColumnSnapshot snapshot;
        snapshot.columnIndex = columnIndex;
        snapshot.folderPath = column.model->folderPath();
        snapshot.columnName = column.model->folderName();

        const int count = column.model->imageCount();
        snapshot.images.reserve(count);
        for (int row = 0; row < count; ++row) {
            VlmAnnotationService::ImageItem item;
            item.columnIndex = columnIndex;
            item.row = row;
            item.folderPath = snapshot.folderPath;
            item.columnName = snapshot.columnName;
            item.imagePath = column.model->imagePathAt(row);
            item.fileName = column.model->fileNameAt(row);
            item.mark = column.model->markAt(row);
            snapshot.images.append(item);
        }

        snapshots.append(snapshot);
    }

    return snapshots;
}

void BrowsePanel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // Dragging the splitter handle (or resizing the window) changes our width;
    // rescale the thumbnails to fill the new column width.
    recomputeThumbnailMetrics();
}

void BrowsePanel::setupUi()
{
    m_rootLayout = new QVBoxLayout(this);
    m_rootLayout->setContentsMargins(14, 14, 14, 8);
    m_rootLayout->setSpacing(8);
    setObjectName(QStringLiteral("browsePanelRoot"));

    // The filter toolbar lives in a flow layout so its controls wrap onto a
    // second row only when the panel is locked to a single narrow column, and
    // collapse back to one row as soon as more folders widen the panel. Labels
    // are omitted — the placeholder text and the combo's default entry already
    // tell the user what each control filters. "模糊匹配" lives in the top
    // command bar (next to "同步尺寸") so it never crowds this row.
    auto *optionsRow = new FlowLayout(0, kFilterRowSpacing, 6);

    m_fileNameFilterEdit = new QLineEdit(this);
    m_fileNameFilterEdit->setObjectName(QStringLiteral("fileNameFilterEdit"));
    m_fileNameFilterEdit->setPlaceholderText(tr("过滤文件名"));
    m_fileNameFilterEdit->setClearButtonEnabled(true);
    m_fileNameFilterEdit->setFixedWidth(kFilterEditWidth);
    optionsRow->addWidget(m_fileNameFilterEdit);

    m_categoryFilterCombo = new QComboBox(this);
    m_categoryFilterCombo->setObjectName(QStringLiteral("categoryFilterComboBox"));
    m_categoryFilterCombo->addItem(tr("全部分类"), QString());
    m_categoryFilterCombo->addItem(tr("未分类"), kUnmarkedCategoryFilter);
    const QStringList categories = ImageMarkManager::categories();
    for (const QString &category : categories) {
        m_categoryFilterCombo->addItem(category, category);
    }
    m_categoryFilterCombo->setFixedWidth(kFilterComboWidth);
    optionsRow->addWidget(m_categoryFilterCombo);

    connect(m_fileNameFilterEdit, &QLineEdit::textChanged,
            this, &BrowsePanel::applyCurrentFilters);
    connect(m_categoryFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int /*index*/) {
        applyCurrentFilters();
    });

    m_scanStatusLabel = new QLabel(tr("Idle"), this);
    m_scanStatusLabel->setStyleSheet(
        "QLabel { color: #6E7785; padding-left: 2px; font-size: 11px; background: transparent; border: none; }");

    m_columnsScrollArea = new QScrollArea(this);
    m_columnsScrollArea->setObjectName(QStringLiteral("compareColumnsScrollArea"));
    m_columnsScrollArea->setWidgetResizable(true);
    m_columnsScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_columnsScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_columnsContainer = new QWidget(m_columnsScrollArea);
    m_columnsContainer->setObjectName(QStringLiteral("compareColumnsContainer"));

    m_columnsLayout = new QHBoxLayout(m_columnsContainer);
    m_columnsLayout->setContentsMargins(0, 0, 0, 0);
    m_columnsLayout->setSpacing(8);
    // No trailing stretch: the columns themselves (stretch 1, MinimumExpanding)
    // fill the panel width so each thumbnail card grows/shrinks with the column.
    m_columnsScrollArea->setWidget(m_columnsContainer);
    m_rootLayout->addLayout(optionsRow);
    m_rootLayout->addWidget(m_columnsScrollArea, 1);
    m_rootLayout->addWidget(m_scanStatusLabel);

    setStyleSheet(
        "QWidget#browsePanelRoot { background-color: #FFFFFF; border: 1px solid #E3E7EC; border-radius: 8px; }"
        "QScrollArea#compareColumnsScrollArea { background-color: #FFFFFF; border: none; }"
        "QWidget#compareColumnsContainer { background-color: #FFFFFF; }"
        "QWidget#compareColumnWidget { background-color: #FFFFFF; }"
        "QWidget#compareColumnHeader { background-color: #FFFFFF; border: none; border-bottom: 1px solid #EEF1F5; }"
        "QLabel#compareColumnHeaderLabel { color: #243041; background: transparent; border: none; }"
        "QLabel#compareColumnProgressLabel { color: #687385; font-size: 11px; background: transparent; border: none; }"
        "QLineEdit#fileNameFilterEdit { background: #FFFFFF; border: 1px solid #DDE4EE; border-radius: 5px; padding: 4px 8px; color: #243041; }"
        "QLineEdit#fileNameFilterEdit:focus { border-color: #0078D4; }"
        "QComboBox#categoryFilterComboBox { background: #FFFFFF; border: 1px solid #DDE4EE; border-radius: 5px; padding: 4px 8px; padding-right: 22px; color: #243041; }"
        "QComboBox#categoryFilterComboBox:hover { border-color: #C7D2E1; }"
        "QComboBox#categoryFilterComboBox:focus { border-color: #0078D4; }"
        "QComboBox#categoryFilterComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right; width: 22px; border: none; background: transparent; }"
        "QComboBox#categoryFilterComboBox::down-arrow { image: url(:/icons/chevron_down.svg); width: 12px; height: 12px; }"
        "QComboBox#categoryFilterComboBox QAbstractItemView { background: #FFFFFF; border: 1px solid #DDE4EE; border-radius: 5px; padding: 2px; outline: none; selection-background-color: #E5F1FB; selection-color: #0078D4; }"
        "QListView#compareColumnListView { background-color: #FFFFFF; border: none; outline: none; }");
}

void BrowsePanel::onFolderAdded(const QString &folderPath, int index)
{
    Q_UNUSED(index);

    ColumnInfo col;
    col.model = new ImageListModel(this);
    col.model->setImageLoader(m_imageLoader);
    col.model->setImageMarkManager(m_markManager);
    if (m_fileNameFilterEdit) {
        col.model->setFileNameFilter(m_fileNameFilterEdit->text());
    }
    if (m_categoryFilterCombo) {
        col.model->setCategoryFilter(currentCategoryFilter());
    }

    col.columnWidget = new QWidget(this);
    col.columnWidget->setObjectName(QStringLiteral("compareColumnWidget"));
    // Expanding (not MinimumExpanding): columns grow to share the panel width, but
    // they must also be allowed to shrink below their preferred width — otherwise
    // the layout would treat each column's preferred width as a hard minimum and
    // the panel could not be dragged narrow when several folders are compared.
    col.columnWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    col.containerLayout = new QVBoxLayout(col.columnWidget);
    col.containerLayout->setContentsMargins(0, 0, 0, 0);
    col.containerLayout->setSpacing(8);

    auto *headerWidget = new QWidget(col.columnWidget);
    headerWidget->setObjectName(QStringLiteral("compareColumnHeader"));
    auto *headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(2, 0, 0, 10);
    headerLayout->setSpacing(8);

    auto *colorSwatch = new QLabel(headerWidget);
    col.colorSwatch = colorSwatch;
    colorSwatch->setFixedSize(12, 12);
    colorSwatch->setStyleSheet(
        QStringLiteral("QLabel { background: %1; border-radius: 3px; border: none; }")
            .arg(browseColor(m_columns.size()).name(QColor::HexRgb)));
    headerLayout->addWidget(colorSwatch, 0, Qt::AlignTop);

    const QString displayName = QDir(folderPath).dirName();
    auto *titleStack = new QVBoxLayout();
    titleStack->setContentsMargins(0, 0, 0, 0);
    titleStack->setSpacing(2);
    auto *headerLabel = new QLabel(headerWidget);
    headerLabel->setObjectName(QStringLiteral("compareColumnHeaderLabel"));
    headerLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    headerLabel->setMinimumWidth(0);
    // Ignored (not Expanding): the elided folder name still fills the header when
    // there is room, but its text width must NOT impose a per-column minimum —
    // otherwise comparing several folders locks the panel to a huge minimum width
    // and the thumbnails can no longer be shrunk by dragging the splitter.
    headerLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    QFont headerFont = headerLabel->font();
    headerFont.setWeight(QFont::DemiBold);
    headerFont.setPointSize(12);
    headerLabel->setFont(headerFont);
    QFontMetrics fm(headerFont);
    headerLabel->setText(fm.elidedText(displayName, Qt::ElideRight, 140));
    headerLabel->setToolTip(displayName);
    titleStack->addWidget(headerLabel);

    col.progressLabel = new QLabel(tr("0 个文件"), col.columnWidget);
    col.progressLabel->setObjectName(QStringLiteral("compareColumnProgressLabel"));
    // The file-count text grows ("1234 / 5678 个文件，扫描中"); without this its
    // size hint would impose a large per-column minimum width and stop the panel
    // (and thus the thumbnails) from shrinking when several folders are compared.
    col.progressLabel->setMinimumWidth(0);
    col.progressLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    titleStack->addWidget(col.progressLabel);
    headerLayout->addLayout(titleStack, 1);

    auto *closeBtn = new QPushButton(QStringLiteral("\u00D7"), headerWidget);
    closeBtn->setObjectName(QStringLiteral("compareColumnCloseButton"));
    closeBtn->setFixedSize(28, 28);
    closeBtn->setToolTip(tr("Remove from comparison"));
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(
        "QPushButton { background-color: transparent; border: none; "
        "padding: 0px; min-height: 0px; "
        "font-size: 16px; font-weight: bold; color: #9E9E9E; border-radius: 14px; }"
        "QPushButton:hover { background-color: #E0E0E0; color: #1A1A1A; }"
        "QPushButton:pressed { background-color: #D1D1D1; color: #1A1A1A; }");
    headerLayout->addWidget(closeBtn);

    QWidget *columnWidgetPtr = col.columnWidget;
    connect(closeBtn, &QPushButton::clicked, this, [this, columnWidgetPtr]() {
        for (int i = 0; i < m_columns.size(); ++i) {
            if (m_columns[i].columnWidget == columnWidgetPtr) {
                m_session->removeFolderAt(i);
                return;
            }
        }
    });

    col.containerLayout->addWidget(headerWidget);

    auto *thumbnailView = new ThumbnailListView(col.columnWidget);
    col.view = thumbnailView;
    col.view->setObjectName(QStringLiteral("compareColumnListView"));
    col.view->setModel(col.model);
    col.view->setItemDelegate(new ThumbnailDelegate(m_columns.size(), &m_metrics, col.view));
    thumbnailView->setMetrics(&m_metrics);
    col.model->setThumbnailSize(QSize(m_metrics.decodeExtent, m_metrics.decodeExtent));
    col.view->setMouseTracking(true);
    col.view->setSelectionMode(QAbstractItemView::NoSelection);
    col.view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    col.view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    col.view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    col.view->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    col.view->setUniformItemSizes(true);
    col.view->setSpacing(6);
    col.view->setFlow(QListView::TopToBottom);
    col.view->setWrapping(false);
    col.view->setResizeMode(QListView::Adjust);
    col.view->setMovement(QListView::Static);
    const int verticalScrollBarWidth = col.view->style()->pixelMetric(
        QStyle::PM_ScrollBarExtent,
        nullptr,
        col.view);
    // Floor each column at the *minimum* thumbnail size (not the baseline), so
    // the splitter remains draggable; once more than kMaxAutoFitColumns are
    // compared, BrowsePanel provides a horizontal scrollbar instead of squeezing
    // every thumbnail down indefinitely.
    col.view->setMinimumWidth(kMinImageWidth
                              + kHorizontalChrome
                              + verticalScrollBarWidth
                              + kScrollAreaSafetyPadding);
    col.containerLayout->addWidget(col.view, 1);

    // Append at the end (no trailing stretch to insert before any more).
    m_columnsLayout->addWidget(col.columnWidget, 1);

    m_columns.append(col);
    ImageListModel *modelPtr = col.model;
    thumbnailView->setColumnIndexCallback([this, modelPtr]() {
        return columnIndexForModel(modelPtr);
    });
    thumbnailView->setSwapCallback([this](int sourceIndex, int targetIndex) {
        if (m_session) {
            m_session->swapFolders(sourceIndex, targetIndex);
        }
    });
    thumbnailView->setMarkCallback([this, modelPtr](const QModelIndex &modelIndex,
                                                    const QString &category,
                                                    Qt::KeyboardModifiers modifiers) {
        const int currentIndex = columnIndexForModel(modelPtr);
        if (currentIndex >= 0 && modelIndex.isValid()) {
            onThumbnailMarkRequested(currentIndex,
                                     modelIndex.row(),
                                     category,
                                     modifiers);
        }
    });
    thumbnailView->setContextMenuCallback([this, modelPtr](const QModelIndex &modelIndex,
                                                           const QPoint &globalPos) {
        if (columnIndexForModel(modelPtr) < 0 || !modelIndex.isValid()) {
            return;
        }

        const QString imagePath = modelIndex.data(ImageListModel::FilePathRole).toString();
        ImageContextMenu::showMenu(imagePath, globalPos, this);
    });
    thumbnailView->setBlankContextMenuCallback([this, modelPtr](const QPoint &globalPos) {
        const int currentIndex = columnIndexForModel(modelPtr);
        if (currentIndex < 0 || !modelPtr) {
            return;
        }
        const QString folderPath = modelPtr->folderPath();
        if (folderPath.isEmpty()) {
            return;
        }
        QMenu menu(this);
        QAction *exportAction = menu.addAction(tr("导出分类…"));
        connect(exportAction, &QAction::triggered, this, [this, folderPath]() {
            emit exportCategoriesRequested(folderPath);
        });
        menu.exec(globalPos);
    });

    connect(col.model, &QAbstractItemModel::rowsInserted,
            this, [this, modelPtr](const QModelIndex &parent, int /*first*/, int last) {
        if (parent.isValid()) {
            return;
        }
        const int currentIndex = columnIndexForModel(modelPtr);
        if (currentIndex < 0) {
            return;
        }

        auto &column = m_columns[currentIndex];
        column.discoveredCount = qMax(column.discoveredCount,
                                      column.model ? column.model->imageCount() : last + 1);
        updateColumnProgressLabel(currentIndex);
        updateGlobalScanStatus();
        scheduleThumbnailRequest(currentIndex, 0);
    });

    connect(col.model, &QAbstractItemModel::modelReset,
            this, [this, modelPtr]() {
        const int currentIndex = columnIndexForModel(modelPtr);
        if (currentIndex < 0) {
            return;
        }

        updateColumnProgressLabel(currentIndex);
        updateGlobalScanStatus();
        requestThumbnailsForColumn(currentIndex);
    });

    connect(col.model, &ImageListModel::folderReady,
            this, [this, modelPtr]() {
        const int currentIndex = columnIndexForModel(modelPtr);
        if (currentIndex >= 0) {
            onFolderReady(currentIndex);
        }
    });

    connect(col.model, &ImageListModel::scanProgressChanged,
            this, [this, modelPtr](int discoveredCount, bool finished) {
        const int currentIndex = columnIndexForModel(modelPtr);
        if (currentIndex < 0) {
            return;
        }
        auto &column = m_columns[currentIndex];
        column.discoveredCount = discoveredCount;
        column.scanFinished = finished;
        updateColumnProgressLabel(currentIndex);
        updateGlobalScanStatus();
    });

    connect(col.view->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this, modelPtr](int /*value*/) {
        const int colIdx = columnIndexForModel(modelPtr);
        if (colIdx < 0) {
            return;
        }
        const auto &c = m_columns[colIdx];
        if (!c.view || !c.model) {
            return;
        }

        const int delayMs = c.view->verticalScrollBar()->isSliderDown() ? 80 : 20;
        scheduleThumbnailRequest(colIdx, delayMs);
    });

    connect(col.view, &QListView::clicked,
            this, [this, modelPtr](const QModelIndex &modelIndex) {
        const int currentIndex = columnIndexForModel(modelPtr);
        if (currentIndex >= 0 && modelIndex.isValid()) {
            onThumbnailActivated(currentIndex,
                                 modelIndex.row(),
                                 QApplication::keyboardModifiers());
        }
    });

    col.model->setFolder(folderPath);
    // A new column narrows every column → rescale all thumbnails to the new width.
    recomputeThumbnailMetrics();
    updateGlobalScanStatus();
}

void BrowsePanel::onFolderReady(int columnIndex)
{
    if (columnIndex < 0 || columnIndex >= m_columns.size()) {
        return;
    }

    auto &col = m_columns[columnIndex];
    col.scanFinished = true;
    col.discoveredCount = col.model ? col.model->unfilteredImageCount() : col.discoveredCount;

    updateColumnProgressLabel(columnIndex);
    updateGlobalScanStatus();
    scheduleThumbnailRequest(columnIndex, 0);
}

void BrowsePanel::onFolderRemoved(const QString &folderPath, int index)
{
    Q_UNUSED(folderPath);

    if (index < 0 || index >= m_columns.size()) {
        return;
    }

    ColumnInfo &col = m_columns[index];
    m_columnsLayout->removeWidget(col.columnWidget);
    delete col.columnWidget;
    delete col.model;

    m_columns.removeAt(index);

    if (m_columns.isEmpty()) {
        stopInterleavedLoading();
        resetSelectionNavigationMode();
    } else if (m_navigationAnchorColumn == index) {
        resetSelectionNavigationMode();
    } else if (m_navigationAnchorColumn > index) {
        --m_navigationAnchorColumn;
    }

    for (int i = index; i < m_columns.size(); ++i) {
        updateColumnVisuals(i);
    }

    // Removing a column widens the survivors → rescale their thumbnails.
    recomputeThumbnailMetrics();
    updateGlobalScanStatus();
    emitSelectionChanged();
}

void BrowsePanel::onFoldersSwapped(int firstIndex, int secondIndex)
{
    if (firstIndex < 0 || firstIndex >= m_columns.size() ||
        secondIndex < 0 || secondIndex >= m_columns.size() ||
        firstIndex == secondIndex) {
        return;
    }

    m_columns.swapItemsAt(firstIndex, secondIndex);
    if (m_navigationAnchorColumn == firstIndex) {
        m_navigationAnchorColumn = secondIndex;
    } else if (m_navigationAnchorColumn == secondIndex) {
        m_navigationAnchorColumn = firstIndex;
    }
    rebuildColumnLayout();
    requestVisibleThumbnailsForAllColumns();
    updateGlobalScanStatus();
    emitSelectionChanged();
}

void BrowsePanel::onSessionCleared()
{
    stopInterleavedLoading();
    clearAllColumns();
    resetSelectionNavigationMode();
    updateGlobalScanStatus();
    emitSelectionChanged();
}

void BrowsePanel::onThumbnailActivated(int clickedCol,
                                       int clickedIdx,
                                       Qt::KeyboardModifiers modifiers)
{
    if (clickedCol < 0 || clickedCol >= m_columns.size()) {
        return;
    }
    if (!m_columns[clickedCol].model ||
        clickedIdx < 0 ||
        clickedIdx >= m_columns[clickedCol].model->imageCount()) {
        return;
    }

    if (modifiers & Qt::ControlModifier) {
        setSelectionNavigationMode(SelectionNavigationMode::Independent, clickedCol);
        clearSelection();
        QList<int> matchedIndices(m_columns.size(), -1);
        for (int c = 0; c < m_columns.size(); ++c) {
            if (clickedIdx < m_columns[c].model->imageCount()) {
                matchedIndices[c] = clickedIdx;
                m_columns[c].model->setSelected(clickedIdx, true);
            }
        }
        alignColumnsToAnchor(clickedCol, clickedIdx, matchedIndices);
    } else if (modifiers & Qt::AltModifier) {
        setSelectionNavigationMode(SelectionNavigationMode::FileNameMatch, clickedCol);
        clearSelection();
        const QString fileName = m_columns[clickedCol].model->fileNameAt(clickedIdx);
        QList<int> matchedIndices(m_columns.size(), -1);

        for (int c = 0; c < m_columns.size(); ++c) {
            // The clicked column must select exactly the row the user clicked.
            // Resolving it by filename would pick the *first* row with that name,
            // selecting the wrong thumbnail when the column has duplicate
            // filenames (and, under fuzzy matching, possibly a different file).
            const int matchIdx = (c == clickedCol)
                ? clickedIdx
                : findFileNameMatchIndex(c, fileName);
            if (matchIdx >= 0) {
                matchedIndices[c] = matchIdx;
                m_columns[c].model->setSelected(matchIdx, true);
            }
        }
        alignColumnsToAnchor(clickedCol, clickedIdx, matchedIndices);
    } else {
        setSelectionNavigationMode(SelectionNavigationMode::Independent, clickedCol);
        clearColumnSelection(clickedCol);
        m_columns[clickedCol].model->setSelected(clickedIdx, true);
    }

    emitSelectionChanged();
}

void BrowsePanel::onThumbnailMarkRequested(int column,
                                           int row,
                                           const QString &category,
                                           Qt::KeyboardModifiers modifiers)
{
    if (column < 0 || column >= m_columns.size() || category.isEmpty()) {
        return;
    }

    ImageListModel *clickedModel = m_columns[column].model;
    if (!clickedModel || row < 0 || row >= clickedModel->imageCount()) {
        return;
    }

    const QString targetCategory = (clickedModel->markAt(row) == category)
        ? QString()
        : category;
    auto refreshAfterMarkChange = [this]() {
        updateAllColumnProgressLabels();
        updateGlobalScanStatus();
        requestVisibleThumbnailsForAllColumns();
        emitSelectionChanged();
    };

    if ((modifiers & Qt::ControlModifier) != 0) {
        bool markedSelection = false;
        for (auto &col : m_columns) {
            if (!col.model) {
                continue;
            }

            const QList<int> selected = col.model->selectedIndices();
            for (int selectedIndex : selected) {
                col.model->setMarkAt(selectedIndex, targetCategory);
                markedSelection = true;
            }
        }

        if (markedSelection) {
            refreshAfterMarkChange();
            return;
        }

        for (auto &col : m_columns) {
            if (col.model && row >= 0 && row < col.model->imageCount()) {
                col.model->setMarkAt(row, targetCategory);
            }
        }
        refreshAfterMarkChange();
        return;
    }

    clickedModel->setMarkAt(row, targetCategory);
    refreshAfterMarkChange();
}

void BrowsePanel::alignColumnsToAnchor(int anchorColumn,
                                       int anchorIndex,
                                       const QList<int> &matchedIndices)
{
    if (anchorColumn < 0 || anchorColumn >= m_columns.size()) {
        return;
    }

    const auto &anchorCol = m_columns[anchorColumn];
    if (!anchorCol.view || !anchorCol.model || anchorIndex < 0 ||
        anchorIndex >= anchorCol.model->imageCount()) {
        return;
    }

    const QModelIndex anchorModelIndex = anchorCol.model->index(anchorIndex);
    QRect anchorRect = anchorCol.view->visualRect(anchorModelIndex);
    if (!anchorRect.isValid()) {
        anchorCol.view->scrollTo(anchorModelIndex, QAbstractItemView::PositionAtCenter);
        anchorRect = anchorCol.view->visualRect(anchorModelIndex);
    }
    const int anchorYInViewport = anchorRect.isValid()
        ? anchorRect.top()
        : anchorIndex * rowExtent(anchorCol.view, m_metrics) - anchorCol.view->verticalScrollBar()->value();

    for (int c = 0; c < m_columns.size(); ++c) {
        if (c == anchorColumn || c >= matchedIndices.size()) {
            continue;
        }

        const int targetIndex = matchedIndices[c];
        auto &targetCol = m_columns[c];
        if (!targetCol.view || !targetCol.model || targetIndex < 0 ||
            targetIndex >= targetCol.model->imageCount()) {
            continue;
        }

        auto *targetScrollBar = targetCol.view->verticalScrollBar();
        const int desiredScroll = qBound(targetScrollBar->minimum(),
                                         targetIndex * rowExtent(targetCol.view, m_metrics) - anchorYInViewport,
                                         targetScrollBar->maximum());
        targetScrollBar->setValue(desiredScroll);
        const QModelIndex targetModelIndex = targetCol.model->index(targetIndex);
        for (int attempt = 0; attempt < 3; ++attempt) {
            const QRect targetRect = targetCol.view->visualRect(targetModelIndex);
            if (!targetRect.isValid()) {
                targetCol.view->scrollTo(targetModelIndex, QAbstractItemView::PositionAtCenter);
                continue;
            }
            const int delta = targetRect.top() - anchorYInViewport;
            if (qAbs(delta) <= 2) {
                break;
            }
            targetScrollBar->setValue(qBound(targetScrollBar->minimum(),
                                             targetScrollBar->value() + delta,
                                             targetScrollBar->maximum()));
        }
        targetCol.model->loadThumbnailsForRange(targetIndex, targetIndex);
    }
}

void BrowsePanel::clearAllColumns()
{
    for (auto &col : m_columns) {
        m_columnsLayout->removeWidget(col.columnWidget);
        delete col.columnWidget;
        delete col.model;
    }
    m_columns.clear();
    if (m_scanStatusLabel) {
        m_scanStatusLabel->setText(tr("Idle"));
    }
    emit scanStatusChanged(tr("Idle"));
    recomputeThumbnailMetrics();
}

void BrowsePanel::recomputeThumbnailMetrics()
{
    // Thumbnail size is a pure function of the per-column width. The first
    // kMaxAutoFitColumns columns fit the available viewport; additional columns
    // keep that usable width and scroll horizontally.
    const int columnCount = qMax(1, m_columns.size());
    const int sizingColumnCount = qMin(columnCount, kMaxAutoFitColumns);
    const int spacing = m_columnsLayout ? m_columnsLayout->spacing() : 8;
    const int scrollBarWidth = style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, this);

    const int panelContent = (m_columnsScrollArea && m_columnsScrollArea->viewport())
        ? m_columnsScrollArea->viewport()->width()
        : width();
    const int perColumn = (panelContent - (sizingColumnCount - 1) * spacing) / sizingColumnCount;
    const int targetImageWidth = perColumn - scrollBarWidth - kCardBreath - kHorizontalChrome;

    const ThumbMetrics next = metricsForImageWidth(targetImageWidth);
    const bool displayChanged = next.imageWidth != m_metrics.imageWidth
        || next.itemHeight != m_metrics.itemHeight;
    const bool bucketChanged = next.decodeExtent != m_metrics.decodeExtent;
    const int targetColumnWidth = qMax(kMinImageWidth
                                           + kHorizontalChrome
                                           + scrollBarWidth
                                           + kScrollAreaSafetyPadding,
                                       next.cardWidth + scrollBarWidth + kCardBreath);
    const bool fixedScrolledColumns = m_columns.size() > kMaxAutoFitColumns;
    bool columnWidthChanged = false;
    for (auto &col : m_columns) {
        if (!col.columnWidget) {
            continue;
        }
        if (col.columnWidget->minimumWidth() != targetColumnWidth) {
            col.columnWidget->setMinimumWidth(targetColumnWidth);
            columnWidthChanged = true;
        }
        const int targetMaximumWidth = fixedScrolledColumns ? targetColumnWidth : QWIDGETSIZE_MAX;
        if (col.columnWidget->maximumWidth() != targetMaximumWidth) {
            col.columnWidget->setMaximumWidth(targetMaximumWidth);
            columnWidthChanged = true;
        }
    }

    if (!displayChanged && !bucketChanged && !columnWidthChanged) {
        return;
    }

    m_metrics = next;

    if (displayChanged) {
        for (auto &col : m_columns) {
            applyMetricsToView(col);
        }
    }

    if (bucketChanged && m_decodeReloadTimer) {
        // Defer the re-decode until the size settles; the live relayout above
        // already rescales the cached pixmaps for instant visual feedback.
        m_decodeReloadTimer->start();
    }
}

void BrowsePanel::applyMetricsToView(ColumnInfo &col)
{
    if (!col.view) {
        return;
    }

    // Keep the row currently at the top of the viewport anchored across the
    // relayout so the content does not jump as the item height changes.
    const QModelIndex topIndex = col.view->indexAt(QPoint(4, 4));

    // QListView caches the uniform item size; a plain doItemsLayout() will not
    // re-query the delegate. Toggling uniform sizes forces it to re-cache from
    // the (changed) sizeHint while staying O(1) per relayout.
    col.view->setUniformItemSizes(false);
    col.view->setUniformItemSizes(true);
    col.view->doItemsLayout();

    if (topIndex.isValid()) {
        col.view->scrollTo(topIndex, QAbstractItemView::PositionAtTop);
    }
    col.view->viewport()->update();
}

void BrowsePanel::onDecodeReloadTimeout()
{
    const QSize bucket(m_metrics.decodeExtent, m_metrics.decodeExtent);
    for (auto &col : m_columns) {
        if (col.model) {
            col.model->setThumbnailSize(bucket);
        }
    }
    // Re-request visible thumbnails at the new bucket; this also cancels stale
    // in-flight work for anything no longer on screen.
    requestVisibleThumbnailsForAllColumns();
}

void BrowsePanel::rebuildColumnLayout()
{
    if (!m_columnsLayout) {
        return;
    }

    while (QLayoutItem *item = m_columnsLayout->takeAt(0)) {
        delete item;
    }

    for (int i = 0; i < m_columns.size(); ++i) {
        updateColumnVisuals(i);
        if (m_columns[i].columnWidget) {
            m_columnsLayout->addWidget(m_columns[i].columnWidget, 1);
        }
    }
    // No trailing stretch: columns fill the panel width (see setupUi).
}

void BrowsePanel::updateColumnVisuals(int columnIndex)
{
    if (columnIndex < 0 || columnIndex >= m_columns.size()) {
        return;
    }

    ColumnInfo &col = m_columns[columnIndex];
    const QColor color = browseColor(columnIndex);
    if (col.colorSwatch) {
        col.colorSwatch->setStyleSheet(
            QStringLiteral("QLabel { background: %1; border-radius: 3px; border: none; }")
                .arg(color.name(QColor::HexRgb)));
    }

    if (col.view) {
        QAbstractItemDelegate *oldDelegate = col.view->itemDelegate();
        col.view->setItemDelegate(new ThumbnailDelegate(columnIndex, &m_metrics, col.view));
        if (oldDelegate && oldDelegate->parent() == col.view) {
            oldDelegate->deleteLater();
        }
        col.view->viewport()->update();
    }
}

void BrowsePanel::clearSelection()
{
    for (auto &col : m_columns) {
        col.model->clearSelection();
    }
}

void BrowsePanel::clearColumnSelection(int column)
{
    if (column < 0 || column >= m_columns.size()) {
        return;
    }

    m_columns[column].model->clearSelection();
}

void BrowsePanel::emitSelectionChanged()
{
    QList<QPair<QString, QString>> selected;

    for (const auto &col : m_columns) {
        QList<int> indices = col.model->selectedIndices();
        for (int idx : indices) {
            selected.append({col.model->folderPath(), col.model->imagePathAt(idx)});
        }
    }

    emit selectionChanged(selected);
    preloadNeighborImagesForSelection();
}

void BrowsePanel::preloadNeighborImagesForSelection()
{
    if (!m_imageLoader) {
        return;
    }

    QSet<QString> preloadSet;
    for (const auto &col : m_columns) {
        if (!col.model) {
            continue;
        }

        const QList<int> selectedIndices = col.model->selectedIndices();
        for (int currentIdx : selectedIndices) {
            for (int offset = 1; offset <= 3; ++offset) {
                const int prevIdx = currentIdx - offset;
                const int nextIdx = currentIdx + offset;

                if (prevIdx >= 0) {
                    const QString prevPath = col.model->imagePathAt(prevIdx);
                    if (!prevPath.isEmpty()) {
                        preloadSet.insert(prevPath);
                    }
                }

                if (nextIdx < col.model->imageCount()) {
                    const QString nextPath = col.model->imagePathAt(nextIdx);
                    if (!nextPath.isEmpty()) {
                        preloadSet.insert(nextPath);
                    }
                }
            }
        }
    }

    if (!preloadSet.isEmpty()) {
        m_imageLoader->requestImageBatch(preloadSet.values());
    }
}

void BrowsePanel::applyCurrentFilters()
{
    resetSelectionNavigationMode();
    clearSelection();

    const QString fileNameFilter = m_fileNameFilterEdit
        ? m_fileNameFilterEdit->text()
        : QString();
    const QString categoryFilter = currentCategoryFilter();

    for (auto &col : m_columns) {
        if (!col.model) {
            continue;
        }
        col.model->setFileNameFilter(fileNameFilter);
        col.model->setCategoryFilter(categoryFilter);
    }

    updateAllColumnProgressLabels();
    updateGlobalScanStatus();
    requestVisibleThumbnailsForAllColumns();
    emitSelectionChanged();
}

QString BrowsePanel::currentCategoryFilter() const
{
    if (!m_categoryFilterCombo) {
        return QString();
    }

    return m_categoryFilterCombo->currentData().toString();
}

void BrowsePanel::updateAllColumnProgressLabels()
{
    for (int i = 0; i < m_columns.size(); ++i) {
        updateColumnProgressLabel(i);
    }
}

int BrowsePanel::findFileNameMatchIndex(int column, const QString &targetFileName) const
{
    if (column < 0 || column >= m_columns.size() || !m_columns[column].model) {
        return -1;
    }

    const auto *model = m_columns[column].model;
    if (!m_fuzzyFileNameMatch) {
        return model->indexOfFileName(targetFileName);
    }

    int bestIndex = -1;
    int bestDistance = std::numeric_limits<int>::max();
    const int targetLen = targetFileName.size();
    for (int i = 0; i < model->imageCount(); ++i) {
        const QString candidate = model->fileNameAt(i);
        // |len(a) - len(b)| is a lower bound on the edit distance, so a candidate
        // whose length already differs by >= the best distance found so far can
        // never win — skip its O(n*m) computation entirely. This keeps the
        // per-click cost low when an exact (or near) match exists, which is the
        // common case (same filenames across folders).
        if (qAbs(targetLen - candidate.size()) >= bestDistance) {
            continue;
        }
        const int distance = levenshteinDistance(targetFileName, candidate);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIndex = i;
            if (bestDistance == 0) {
                break; // exact match — cannot do better
            }
        }
    }

    return bestIndex;
}

int BrowsePanel::levenshteinDistance(const QString &a, const QString &b) const
{
    const int n = a.size();
    const int m = b.size();

    std::vector<int> prev(m + 1, 0);
    std::vector<int> curr(m + 1, 0);

    for (int j = 0; j <= m; ++j) {
        prev[j] = j;
    }

    for (int i = 1; i <= n; ++i) {
        curr[0] = i;
        for (int j = 1; j <= m; ++j) {
            const int cost = (a.at(i - 1) == b.at(j - 1)) ? 0 : 1;
            curr[j] = qMin(qMin(prev[j] + 1, curr[j - 1] + 1), prev[j - 1] + cost);
        }
        std::swap(prev, curr);
    }

    return prev[m];
}

int BrowsePanel::columnIndexForModel(const ImageListModel *model) const
{
    if (!model) {
        return -1;
    }

    for (int i = 0; i < m_columns.size(); ++i) {
        if (m_columns[i].model == model) {
            return i;
        }
    }

    return -1;
}

void BrowsePanel::navigateNext()
{
    navigateSelection(+1);
}

void BrowsePanel::navigatePrevious()
{
    navigateSelection(-1);
}

void BrowsePanel::navigateSelection(int delta)
{
    bool anyChanged = false;
    if (m_selectionNavigationMode == SelectionNavigationMode::FileNameMatch) {
        anyChanged = navigateFileNameMatchedSelection(delta);
    } else {
        anyChanged = navigateIndependentSelection(delta);
    }

    if (anyChanged) {
        requestVisibleThumbnailsForAllColumns();
        emitSelectionChanged();
    }
}

bool BrowsePanel::navigateFileNameMatchedSelection(int delta)
{
    if (m_navigationAnchorColumn < 0 ||
        m_navigationAnchorColumn >= m_columns.size()) {
        resetSelectionNavigationMode();
        return navigateIndependentSelection(delta);
    }

    ColumnInfo &anchorCol = m_columns[m_navigationAnchorColumn];
    if (!anchorCol.model) {
        resetSelectionNavigationMode();
        return navigateIndependentSelection(delta);
    }

    const int currentIdx = primarySelectedIndex(anchorCol.model);
    if (currentIdx < 0) {
        resetSelectionNavigationMode();
        return navigateIndependentSelection(delta);
    }

    const int newAnchorIdx = currentIdx + delta;
    if (newAnchorIdx < 0 || newAnchorIdx >= anchorCol.model->imageCount()) {
        return false;
    }

    const QString targetFileName = anchorCol.model->fileNameAt(newAnchorIdx);
    QList<int> matchedIndices(m_columns.size(), -1);
    matchedIndices[m_navigationAnchorColumn] = newAnchorIdx;

    for (int c = 0; c < m_columns.size(); ++c) {
        if (c == m_navigationAnchorColumn || !m_columns[c].model) {
            continue;
        }

        matchedIndices[c] = findFileNameMatchIndex(c, targetFileName);
    }

    clearSelection();
    for (int c = 0; c < m_columns.size(); ++c) {
        const int targetIdx = matchedIndices.value(c, -1);
        if (m_columns[c].model && targetIdx >= 0 &&
            targetIdx < m_columns[c].model->imageCount()) {
            m_columns[c].model->setSelected(targetIdx, true);
        }
    }

    if (anchorCol.view) {
        anchorCol.view->scrollTo(anchorCol.model->index(newAnchorIdx),
                                 QAbstractItemView::PositionAtCenter);
    }
    alignColumnsToAnchor(m_navigationAnchorColumn, newAnchorIdx, matchedIndices);
    return true;
}

bool BrowsePanel::navigateIndependentSelection(int delta)
{
    bool anyChanged = false;
    for (int c = 0; c < m_columns.size(); ++c) {
        auto &col = m_columns[c];
        if (!col.model) {
            continue;
        }

        const int currentIdx = primarySelectedIndex(col.model);
        if (currentIdx < 0) {
            continue;
        }

        const int newIdx = currentIdx + delta;

        if (newIdx < 0 || newIdx >= col.model->imageCount()) {
            continue;
        }

        col.model->clearSelection();
        col.model->setSelected(newIdx, true);
        if (col.view) {
            col.view->scrollTo(col.model->index(newIdx), QAbstractItemView::PositionAtCenter);
        }
        anyChanged = true;
    }

    return anyChanged;
}

void BrowsePanel::setSelectionNavigationMode(SelectionNavigationMode mode, int anchorColumn)
{
    m_selectionNavigationMode = mode;
    m_navigationAnchorColumn = anchorColumn;
}

void BrowsePanel::resetSelectionNavigationMode()
{
    m_selectionNavigationMode = SelectionNavigationMode::Independent;
    m_navigationAnchorColumn = -1;
}

void BrowsePanel::startInterleavedLoading()
{
    if (m_interleavedLoadTimer && m_interleavedLoadTimer->isActive()) {
        return;
    }

    if (!m_interleavedLoadTimer) {
        m_interleavedLoadTimer = new QTimer(this);
        m_interleavedLoadTimer->setInterval(0);
        connect(m_interleavedLoadTimer, &QTimer::timeout,
                this, &BrowsePanel::onInterleavedLoadTick);
    }

    m_interleavedLoadTimer->start();
}

void BrowsePanel::stopInterleavedLoading()
{
    if (m_interleavedLoadTimer) {
        m_interleavedLoadTimer->stop();
    }
}

void BrowsePanel::onInterleavedLoadTick()
{
    requestVisibleThumbnailsForAllColumns();
    stopInterleavedLoading();
}

QPair<int, int> BrowsePanel::visibleRangeForColumn(const ColumnInfo &column) const
{
    if (!column.view || !column.model || column.model->imageCount() <= 0) {
        return {0, -1};
    }

    const int scrollY = column.view->verticalScrollBar()->value();
    const int viewportH = column.view->viewport()->height();
    const int extent = qMax(1, rowExtent(column.view, m_metrics));
    const int firstVisible = qMax(0, scrollY / extent - 2);
    const int lastVisible = qMin(column.model->imageCount() - 1,
                                 (scrollY + viewportH) / extent + 3);
    return {firstVisible, lastVisible};
}

QPair<int, int> BrowsePanel::prefetchRangeForColumn(const ColumnInfo &column) const
{
    auto [firstVisible, lastVisible] = visibleRangeForColumn(column);
    if (lastVisible < firstVisible || !column.model) {
        return {0, -1};
    }

    const int visibleRows = lastVisible - firstVisible + 1;
    const int margin = qMax(kPrefetchMinimumRows, visibleRows * 3);
    return {
        qMax(0, firstVisible - margin),
        qMin(column.model->imageCount() - 1, lastVisible + margin)
    };
}

void BrowsePanel::refreshAllVisibleThumbnails()
{
    requestVisibleThumbnailsForAllColumns();
}

void BrowsePanel::requestVisibleThumbnailsForAllColumns()
{
    for (int i = 0; i < m_columns.size(); ++i) {
        requestThumbnailsForColumn(i);
    }

    cancelStaleThumbnailRequests();
}

void BrowsePanel::cancelStaleThumbnailRequests()
{
    if (!m_imageLoader) {
        return;
    }
    QSet<QString> keep = aggregateVisiblePaths();
    if (keep == m_lastCancelKeepSet) {
        // Same visible+prefetch set as last time: re-filtering the loader's
        // queues would keep exactly the same entries and only churn the
        // cancellation generation (forcing in-flight workers to re-check the
        // identical keep-set under a mutex). Skip it.
        return;
    }
    m_lastCancelKeepSet = keep;
    m_imageLoader->cancelThumbnailRequestsExcept(keep);
}

void BrowsePanel::scheduleThumbnailRequest(int columnIndex, int delayMs)
{
    if (columnIndex < 0 || columnIndex >= m_columns.size()) {
        return;
    }

    auto &col = m_columns[columnIndex];
    if (!col.model || !col.view || col.thumbnailRequestScheduled) {
        return;
    }

    col.thumbnailRequestScheduled = true;
    ImageListModel *modelPtr = col.model;
    QTimer::singleShot(qMax(0, delayMs), this, [this, modelPtr]() {
        const int currentIndex = columnIndexForModel(modelPtr);
        if (currentIndex < 0) {
            return;
        }
        m_columns[currentIndex].thumbnailRequestScheduled = false;
        requestThumbnailsForColumn(currentIndex);
        cancelStaleThumbnailRequests();
    });
}

void BrowsePanel::requestThumbnailsForColumn(int columnIndex)
{
    if (columnIndex < 0 || columnIndex >= m_columns.size()) {
        return;
    }

    auto &col = m_columns[columnIndex];
    if (!col.model || !col.view || col.model->imageCount() <= 0) {
        return;
    }

    auto [firstVisible, lastVisible] = visibleRangeForColumn(col);
    if (lastVisible < firstVisible) {
        return;
    }
    auto [firstPrefetch, lastPrefetch] = prefetchRangeForColumn(col);

    // On-screen rows at visible priority so they win the bounded decode pool;
    // the off-screen prefetch margins at prefetch priority so a large margin
    // (or a fast scroll) can't starve what the user is actually looking at.
    // The same set of thumbnails is still requested — only their priority
    // differs — and an item bumped from prefetch into view is re-requested at
    // visible priority (ImageLoader upgrades the pending entry).
    col.model->loadThumbnailsForRange(firstVisible, lastVisible, /*prefetchPriority=*/false);
    if (firstPrefetch < firstVisible) {
        col.model->loadThumbnailsForRange(firstPrefetch, firstVisible - 1, /*prefetchPriority=*/true);
    }
    if (lastPrefetch > lastVisible) {
        col.model->loadThumbnailsForRange(lastVisible + 1, lastPrefetch, /*prefetchPriority=*/true);
    }
}

QSet<QString> BrowsePanel::aggregateVisiblePaths() const
{
    QSet<QString> visiblePaths;
    for (const auto &col : m_columns) {
        if (!col.model || !col.view || col.model->imageCount() <= 0) {
            continue;
        }
        auto [firstVisible, lastVisible] = prefetchRangeForColumn(col);
        for (int i = firstVisible; i <= lastVisible; ++i) {
            const QString path = col.model->imagePathAt(i);
            if (!path.isEmpty()) {
                visiblePaths.insert(path);
            }
        }
    }
    return visiblePaths;
}

void BrowsePanel::updateColumnProgressLabel(int columnIndex)
{
    if (columnIndex < 0 || columnIndex >= m_columns.size()) {
        return;
    }

    auto &col = m_columns[columnIndex];
    if (!col.progressLabel) {
        return;
    }

    const int visibleCount = col.model ? col.model->imageCount() : col.discoveredCount;
    const int totalCount = col.model
        ? qMax(col.discoveredCount, col.model->unfilteredImageCount())
        : col.discoveredCount;
    const bool filtered = col.model && col.model->hasActiveFilters();
    const QString countText = filtered
        ? tr("%1 / %2 个文件").arg(visibleCount).arg(totalCount)
        : tr("%1 个文件").arg(totalCount);

    if (col.scanFinished) {
        col.progressLabel->setText(countText);
    } else {
        col.progressLabel->setText(tr("%1，扫描中").arg(countText));
    }
}

void BrowsePanel::updateGlobalScanStatus()
{
    int discoveredTotal = 0;
    int visibleTotal = 0;
    int scanningCount = 0;
    bool filtersActive = false;
    for (const auto &col : m_columns) {
        const int totalCount = col.model
            ? qMax(col.discoveredCount, col.model->unfilteredImageCount())
            : col.discoveredCount;
        discoveredTotal += totalCount;
        visibleTotal += col.model ? col.model->imageCount() : totalCount;
        filtersActive = filtersActive || (col.model && col.model->hasActiveFilters());
        if (!col.scanFinished) {
            ++scanningCount;
        }
    }

    const QString statusText = m_columns.isEmpty()
        ? tr("Idle")
        : (scanningCount > 0
            ? tr("Scanning %1 folder(s), discovered %2 image(s)")
                  .arg(scanningCount)
                  .arg(discoveredTotal)
            : (filtersActive
                ? tr("Filter active, showing %1 of %2 image(s)")
                      .arg(visibleTotal)
                      .arg(discoveredTotal)
                : tr("Scan complete, discovered %1 image(s)").arg(discoveredTotal)));

    if (m_scanStatusLabel) {
        m_scanStatusLabel->setText(statusText);
    }
    emit scanStatusChanged(statusText);
}
