#include "BrowsePanel.h"
#include "ImageContextMenu.h"
#include "models/CompareSession.h"
#include "models/ImageListModel.h"
#include "services/ImageLoader.h"
#include "services/ImageMarkManager.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
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
#include <QListView>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollBar>
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
constexpr int kThumbnailCardWidth = 166;
constexpr int kThumbnailImageWidth = 154;
constexpr int kThumbnailImageHeight = 96;
constexpr int kThumbnailItemHeight = 142;
constexpr int kColumnHorizontalMargins = 16;
constexpr int kScrollAreaSafetyPadding = 4;
constexpr int kPrefetchMinimumRows = 32;
constexpr int kMarkButtonSize = 18;
constexpr int kMarkButtonGap = 3;
constexpr int kMarkButtonTopMargin = 10;
constexpr int kMarkButtonRightMargin = 10;
const char kFolderOrderMimeType[] = "application/x-eplayer-folder-index";

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

QRect thumbnailCardRect(const QRect &itemRect)
{
    const int cardX = itemRect.x() + qMax(0, (itemRect.width() - kThumbnailCardWidth) / 2);
    return QRect(cardX, itemRect.y() + 2, kThumbnailCardWidth, kThumbnailItemHeight - 4);
}

QRect markButtonRect(const QRect &itemRect, int categoryIndex)
{
    const QRect cardRect = thumbnailCardRect(itemRect);
    const int categoryCount = ImageMarkManager::categories().size();
    const int totalWidth = categoryCount * kMarkButtonSize
                           + (categoryCount - 1) * kMarkButtonGap;
    const int x = cardRect.x() + cardRect.width() - kMarkButtonRightMargin
                  - totalWidth + categoryIndex * (kMarkButtonSize + kMarkButtonGap);
    const int y = cardRect.y() + kMarkButtonTopMargin;
    return QRect(x, y, kMarkButtonSize, kMarkButtonSize);
}

QString markCategoryAtPosition(const QRect &itemRect, const QPoint &pos)
{
    const QStringList categories = ImageMarkManager::categories();
    for (int i = 0; i < categories.size(); ++i) {
        if (markButtonRect(itemRect, i).contains(pos)) {
            return categories.at(i);
        }
    }
    return QString();
}

void paintMarkButtons(QPainter *painter,
                      const QRect &itemRect,
                      const QString &currentMark,
                      bool hovered)
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
        const QRect rect = markButtonRect(itemRect, i);
        const bool active = (category == currentMark);

        painter->setPen(QPen(active ? QColor(0x00, 0x78, 0xD4) : QColor(0xC8, 0xC8, 0xC8), 1));
        painter->setBrush(active ? QColor(0x00, 0x78, 0xD4) : QColor(255, 255, 255, 230));
        painter->drawRoundedRect(rect, 4, 4);
        painter->setPen(active ? Qt::white : QColor(0x42, 0x42, 0x42));
        painter->drawText(rect, Qt::AlignCenter, category);
    }
}

class ThumbnailListView final : public QListView
{
public:
    using MarkCallback = std::function<void(const QModelIndex &,
                                            const QString &,
                                            Qt::KeyboardModifiers)>;
    using ContextMenuCallback = std::function<void(const QModelIndex &, const QPoint &)>;
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

    void setColumnIndexCallback(ColumnIndexCallback callback)
    {
        m_columnIndexCallback = std::move(callback);
    }

    void setSwapCallback(SwapCallback callback)
    {
        m_swapCallback = std::move(callback);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            const QModelIndex modelIndex = indexAt(event->pos());
            if (modelIndex.isValid()) {
                const QString category = markCategoryAtPosition(visualRect(modelIndex),
                                                                event->pos());
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
        if (!modelIndex.isValid() || !m_contextMenuCallback) {
            return false;
        }

        m_contextMenuCallback(modelIndex, event->globalPos());
        event->accept();
        return true;
    }

    MarkCallback m_markCallback;
    ContextMenuCallback m_contextMenuCallback;
    ColumnIndexCallback m_columnIndexCallback;
    SwapCallback m_swapCallback;
    QPoint m_dragStartPos;
    QModelIndex m_dragCandidateIndex;
};

class ThumbnailDelegate final : public QStyledItemDelegate
{
public:
    explicit ThumbnailDelegate(int colorIndex, QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
        , m_accentColor(browseColor(colorIndex))
    {
    }

    QSize sizeHint(const QStyleOptionViewItem & /*option*/,
                   const QModelIndex & /*index*/) const override
    {
        return QSize(kThumbnailCardWidth + 16, kThumbnailItemHeight);
    }

    void paint(QPainter *painter,
               const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        const bool selected = index.data(ImageListModel::IsSelectedRole).toBool();
        const QString currentMark = index.data(ImageListModel::MarkRole).toString();
        const bool hovered = option.state & QStyle::State_MouseOver;
        const QRect cardRect = thumbnailCardRect(option.rect);

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
                              kThumbnailImageWidth,
                              kThumbnailImageHeight);

        QPainterPath clipPath;
        clipPath.addRoundedRect(thumbArea, 6, 6);
        painter->setClipPath(clipPath);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0xF5, 0xF5, 0xF5));
        painter->drawRect(thumbArea);

        const QVariant thumbVar = index.data(ImageListModel::ThumbnailRole);
        const QImage thumbnail = thumbVar.canConvert<QImage>() ? thumbVar.value<QImage>() : QImage();
        if (!thumbnail.isNull()) {
            const QImage cover = thumbnail.scaled(thumbArea.size(),
                                                  Qt::KeepAspectRatioByExpanding,
                                                  Qt::SmoothTransformation);
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
                             kThumbnailImageWidth,
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

        paintMarkButtons(painter, option.rect, currentMark, hovered);

        painter->restore();
    }

private:
    QColor m_accentColor;
};

int rowExtent(const QListView *view)
{
    return kThumbnailItemHeight + (view ? view->spacing() : 0);
}
}

BrowsePanel::BrowsePanel(CompareSession *session, ImageLoader *imageLoader,
                         QWidget *parent)
    : QWidget(parent)
    , m_session(session)
    , m_imageLoader(imageLoader)
{
    setupUi();

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

void BrowsePanel::setupUi()
{
    m_rootLayout = new QVBoxLayout(this);
    m_rootLayout->setContentsMargins(14, 14, 14, 8);
    m_rootLayout->setSpacing(8);
    setObjectName(QStringLiteral("browsePanelRoot"));

    auto *optionsRow = new QHBoxLayout();
    optionsRow->setContentsMargins(0, 0, 0, 0);
    optionsRow->setSpacing(8);

    m_fuzzyFileNameCheckBox = new QCheckBox(tr("Fuzzy filename match"), this);
    m_fuzzyFileNameCheckBox->setObjectName(QStringLiteral("fuzzyFileNameCheckBox"));
    m_fuzzyFileNameCheckBox->setChecked(false);
    m_fuzzyFileNameCheckBox->setToolTip(
        tr("When enabled, Alt+Click will match the closest filename in each compared folder."));
    m_fuzzyFileNameCheckBox->setVisible(false);
    optionsRow->addWidget(m_fuzzyFileNameCheckBox);
    optionsRow->addStretch();

    m_scanStatusLabel = new QLabel(tr("Idle"), this);
    m_scanStatusLabel->setStyleSheet(
        "QLabel { color: #6E7785; padding-left: 2px; font-size: 11px; background: transparent; border: none; }");

    m_columnsLayout = new QHBoxLayout();
    m_columnsLayout->setContentsMargins(0, 0, 0, 0);
    m_columnsLayout->setSpacing(8);
    m_columnsLayout->addStretch();
    m_rootLayout->addLayout(m_columnsLayout, 1);
    m_rootLayout->addWidget(m_scanStatusLabel);

    setStyleSheet(
        "QWidget#browsePanelRoot { background-color: #FFFFFF; border: 1px solid #E3E7EC; border-radius: 8px; }"
        "QWidget#compareColumnWidget { background-color: #FFFFFF; }"
        "QWidget#compareColumnHeader { background-color: #FFFFFF; border: none; border-bottom: 1px solid #EEF1F5; }"
        "QLabel#compareColumnHeaderLabel { color: #243041; background: transparent; border: none; }"
        "QLabel#compareColumnProgressLabel { color: #687385; font-size: 11px; background: transparent; border: none; }"
        "QListView#compareColumnListView { background-color: #FFFFFF; border: none; outline: none; }");
}

void BrowsePanel::onFolderAdded(const QString &folderPath, int index)
{
    Q_UNUSED(index);

    ColumnInfo col;
    col.model = new ImageListModel(this);
    col.model->setImageLoader(m_imageLoader);
    col.model->setImageMarkManager(m_markManager);

    col.columnWidget = new QWidget(this);
    col.columnWidget->setObjectName(QStringLiteral("compareColumnWidget"));
    col.columnWidget->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Expanding);

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
    headerLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
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
    col.view->setItemDelegate(new ThumbnailDelegate(m_columns.size(), col.view));
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
    col.view->setMinimumWidth(kThumbnailCardWidth
                              + kColumnHorizontalMargins
                              + verticalScrollBarWidth
                              + kScrollAreaSafetyPadding);
    col.containerLayout->addWidget(col.view, 1);

    const int insertPos = m_columnsLayout->count() - 1;
    m_columnsLayout->insertWidget(insertPos, col.columnWidget, 1);

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
    updateGlobalScanStatus();
}

void BrowsePanel::onFolderReady(int columnIndex)
{
    if (columnIndex < 0 || columnIndex >= m_columns.size()) {
        return;
    }

    auto &col = m_columns[columnIndex];
    col.scanFinished = true;
    col.discoveredCount = col.model ? col.model->imageCount() : col.discoveredCount;

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
            const int matchIdx = findFileNameMatchIndex(c, fileName);
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

    if ((modifiers & Qt::ControlModifier) != 0) {
        bool markedSelection = false;
        for (auto &col : m_columns) {
            if (!col.model) {
                continue;
            }

            const QList<int> selected = col.model->selectedIndices();
            for (int selectedIndex : selected) {
                col.model->setMarkAt(selectedIndex, category);
                markedSelection = true;
            }
        }

        if (markedSelection) {
            return;
        }

        for (auto &col : m_columns) {
            if (col.model && row >= 0 && row < col.model->imageCount()) {
                col.model->setMarkAt(row, category);
            }
        }
        return;
    }

    ImageListModel *model = m_columns[column].model;
    if (!model || row < 0 || row >= model->imageCount()) {
        return;
    }

    model->setMarkAt(row, category);
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
        : anchorIndex * rowExtent(anchorCol.view) - anchorCol.view->verticalScrollBar()->value();

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
                                         targetIndex * rowExtent(targetCol.view) - anchorYInViewport,
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
    m_columnsLayout->addStretch();
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
        col.view->setItemDelegate(new ThumbnailDelegate(columnIndex, col.view));
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

int BrowsePanel::findFileNameMatchIndex(int column, const QString &targetFileName) const
{
    if (column < 0 || column >= m_columns.size() || !m_columns[column].model) {
        return -1;
    }

    const auto *model = m_columns[column].model;
    if (!m_fuzzyFileNameCheckBox || !m_fuzzyFileNameCheckBox->isChecked()) {
        return model->indexOfFileName(targetFileName);
    }

    int bestIndex = -1;
    int bestDistance = std::numeric_limits<int>::max();
    for (int i = 0; i < model->imageCount(); ++i) {
        const int distance = levenshteinDistance(targetFileName, model->fileNameAt(i));
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIndex = i;
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
    const int extent = qMax(1, rowExtent(column.view));
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

void BrowsePanel::requestVisibleThumbnailsForAllColumns()
{
    for (int i = 0; i < m_columns.size(); ++i) {
        requestThumbnailsForColumn(i);
    }

    if (m_imageLoader) {
        m_imageLoader->cancelThumbnailRequestsExcept(aggregateVisiblePaths());
    }
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
        if (m_imageLoader) {
            m_imageLoader->cancelThumbnailRequestsExcept(aggregateVisiblePaths());
        }
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

    auto [firstVisible, lastVisible] = prefetchRangeForColumn(col);
    if (lastVisible >= firstVisible) {
        col.model->loadThumbnailsForRange(firstVisible, lastVisible);
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

    if (col.scanFinished) {
        col.progressLabel->setText(tr("%1 个文件").arg(col.discoveredCount));
    } else {
        col.progressLabel->setText(tr("%1 个文件，扫描中").arg(col.discoveredCount));
    }
}

void BrowsePanel::updateGlobalScanStatus()
{
    int discoveredTotal = 0;
    int scanningCount = 0;
    for (const auto &col : m_columns) {
        discoveredTotal += col.discoveredCount;
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
            : tr("Scan complete, discovered %1 image(s)").arg(discoveredTotal));

    if (m_scanStatusLabel) {
        m_scanStatusLabel->setText(statusText);
    }
    emit scanStatusChanged(statusText);
}
