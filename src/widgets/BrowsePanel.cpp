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
#include <QDir>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
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

#include <functional>
#include <limits>
#include <vector>

namespace
{
constexpr int kThumbnailCardWidth = 194;
constexpr int kThumbnailImageSize = 180;
constexpr int kThumbnailItemHeight = 222;
constexpr int kColumnHorizontalMargins = 16;
constexpr int kScrollAreaSafetyPadding = 4;
constexpr int kPrefetchMinimumRows = 32;
constexpr int kMarkButtonSize = 18;
constexpr int kMarkButtonGap = 3;
constexpr int kMarkButtonTopMargin = 10;
constexpr int kMarkButtonRightMargin = 10;

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
                      const QString &currentMark)
{
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

    explicit ThumbnailListView(QWidget *parent = nullptr)
        : QListView(parent)
    {
    }

    void setMarkCallback(MarkCallback callback)
    {
        m_markCallback = std::move(callback);
    }

    void setContextMenuCallback(ContextMenuCallback callback)
    {
        m_contextMenuCallback = std::move(callback);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::RightButton) {
            const QModelIndex modelIndex = indexAt(event->pos());
            if (modelIndex.isValid() && m_contextMenuCallback) {
                m_contextMenuCallback(modelIndex, event->globalPosition().toPoint());
                event->accept();
                return;
            }
        }

        if (event->button() == Qt::LeftButton) {
            const QModelIndex modelIndex = indexAt(event->pos());
            if (modelIndex.isValid()) {
                const QString category = markCategoryAtPosition(visualRect(modelIndex),
                                                                event->pos());
                if (!category.isEmpty()) {
                    if (m_markCallback) {
                        m_markCallback(modelIndex, category, event->modifiers());
                    }
                    event->accept();
                    return;
                }
            }
        }

        QListView::mousePressEvent(event);
    }

    void contextMenuEvent(QContextMenuEvent *event) override
    {
        const QModelIndex modelIndex = indexAt(event->pos());
        if (modelIndex.isValid() && m_contextMenuCallback) {
            m_contextMenuCallback(modelIndex, event->globalPos());
            event->accept();
            return;
        }

        QListView::contextMenuEvent(event);
    }

private:
    MarkCallback m_markCallback;
    ContextMenuCallback m_contextMenuCallback;
};

class ThumbnailDelegate final : public QStyledItemDelegate
{
public:
    explicit ThumbnailDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
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
            painter->setPen(QPen(QColor(0x00, 0x78, 0xD4), 2));
            painter->setBrush(QColor(0xE5, 0xF1, 0xFB));
        } else if (hovered) {
            painter->setPen(QPen(QColor(0xD1, 0xD1, 0xD1), 1));
            painter->setBrush(QColor(0xFF, 0xFF, 0xFF));
        } else {
            painter->setPen(QPen(QColor(0xE0, 0xE0, 0xE0), 1));
            painter->setBrush(QColor(0xFF, 0xFF, 0xFF));
        }
        painter->drawRoundedRect(cardRect.adjusted(1, 1, -1, -1), 8, 8);

        const QRect thumbArea(cardRect.x() + 7,
                              cardRect.y() + 7,
                              kThumbnailImageSize,
                              kThumbnailImageSize);

        QPainterPath clipPath;
        clipPath.addRoundedRect(thumbArea, 6, 6);
        painter->setClipPath(clipPath);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0xF5, 0xF5, 0xF5));
        painter->drawRect(thumbArea);

        const QVariant thumbVar = index.data(ImageListModel::ThumbnailRole);
        const QImage thumbnail = thumbVar.canConvert<QImage>() ? thumbVar.value<QImage>() : QImage();
        if (!thumbnail.isNull()) {
            const int x = thumbArea.x() + (thumbArea.width() - thumbnail.width()) / 2;
            const int y = thumbArea.y() + (thumbArea.height() - thumbnail.height()) / 2;
            painter->drawImage(QPoint(x, y), thumbnail);
        } else {
            painter->setPen(QColor(0x9E, 0x9E, 0x9E));
            QFont placeholderFont = painter->font();
            placeholderFont.setPointSize(10);
            painter->setFont(placeholderFont);
            painter->drawText(thumbArea, Qt::AlignCenter, QObject::tr("Loading..."));
        }
        painter->setClipping(false);

        const QRect textArea(cardRect.x() + 7,
                             thumbArea.bottom() + 4,
                             kThumbnailImageSize,
                             24);
        painter->setPen(QColor(0x61, 0x61, 0x61));
        QFont font = painter->font();
        font.setPointSize(10);
        painter->setFont(font);
        QFontMetrics fm(font);
        const QString fileName = index.data(ImageListModel::FileNameRole).toString();
        painter->drawText(textArea,
                          Qt::AlignCenter,
                          fm.elidedText(fileName, Qt::ElideMiddle, textArea.width()));

        paintMarkButtons(painter, option.rect, currentMark);

        painter->restore();
    }
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
    m_rootLayout->setContentsMargins(8, 8, 8, 8);
    m_rootLayout->setSpacing(8);

    auto *optionsRow = new QHBoxLayout();
    optionsRow->setContentsMargins(0, 0, 0, 0);
    optionsRow->setSpacing(8);

    m_fuzzyFileNameCheckBox = new QCheckBox(tr("Fuzzy filename match"), this);
    m_fuzzyFileNameCheckBox->setObjectName(QStringLiteral("fuzzyFileNameCheckBox"));
    m_fuzzyFileNameCheckBox->setChecked(false);
    m_fuzzyFileNameCheckBox->setToolTip(
        tr("When enabled, Alt+Click will match the closest filename in each compared folder."));
    optionsRow->addWidget(m_fuzzyFileNameCheckBox);
    optionsRow->addStretch();
    m_rootLayout->addLayout(optionsRow);

    m_scanStatusLabel = new QLabel(tr("Idle"), this);
    m_scanStatusLabel->setStyleSheet(
        "QLabel { color: #6E6E6E; padding-left: 2px; background: transparent; border: none; }");
    m_rootLayout->addWidget(m_scanStatusLabel);

    m_columnsLayout = new QHBoxLayout();
    m_columnsLayout->setContentsMargins(8, 8, 8, 8);
    m_columnsLayout->setSpacing(8);
    m_columnsLayout->addStretch();
    m_rootLayout->addLayout(m_columnsLayout, 1);

    setStyleSheet("BrowsePanel { background-color: #F5F5F5; }");
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
    col.columnWidget->setStyleSheet("QWidget#compareColumnWidget { background-color: #F5F5F5; }");
    col.columnWidget->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Expanding);

    col.containerLayout = new QVBoxLayout(col.columnWidget);
    col.containerLayout->setContentsMargins(8, 8, 8, 8);
    col.containerLayout->setSpacing(8);

    auto *headerWidget = new QWidget(col.columnWidget);
    auto *headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(12, 8, 8, 8);
    headerLayout->setSpacing(8);

    const QString displayName = QDir(folderPath).dirName();
    auto *headerLabel = new QLabel(headerWidget);
    headerLabel->setObjectName(QStringLiteral("compareColumnHeaderLabel"));
    headerLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    headerLabel->setMinimumWidth(0);
    headerLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QFont headerFont = headerLabel->font();
    headerFont.setWeight(QFont::DemiBold);
    headerFont.setPointSize(12);
    headerLabel->setFont(headerFont);
    headerLabel->setStyleSheet(
        "QLabel { color: #1A1A1A; background: transparent; border: none; }");
    QFontMetrics fm(headerFont);
    headerLabel->setText(fm.elidedText(displayName, Qt::ElideRight, 140));
    headerLabel->setToolTip(displayName);
    headerLayout->addWidget(headerLabel, 1);

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

    headerWidget->setStyleSheet(
        "QWidget { background-color: #FFFFFF; border-radius: 8px; }");
    col.containerLayout->addWidget(headerWidget);

    col.progressLabel = new QLabel(tr("Discovered: 0"), col.columnWidget);
    col.progressLabel->setAlignment(Qt::AlignCenter);
    col.progressLabel->setStyleSheet(
        "QLabel { color: #7D7D7D; padding: 4px; background: transparent; border: none; }");
    col.containerLayout->addWidget(col.progressLabel);

    auto *thumbnailView = new ThumbnailListView(col.columnWidget);
    col.view = thumbnailView;
    col.view->setObjectName(QStringLiteral("compareColumnListView"));
    col.view->setModel(col.model);
    col.view->setItemDelegate(new ThumbnailDelegate(col.view));
    col.view->setMouseTracking(true);
    col.view->setSelectionMode(QAbstractItemView::NoSelection);
    col.view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    col.view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    col.view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    col.view->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    col.view->setUniformItemSizes(true);
    col.view->setSpacing(8);
    col.view->setFlow(QListView::TopToBottom);
    col.view->setWrapping(false);
    col.view->setResizeMode(QListView::Adjust);
    col.view->setMovement(QListView::Static);
    col.view->setStyleSheet(
        "QListView { background-color: #F5F5F5; border: none; outline: none; }");
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
    thumbnailView->setContextMenuCallback([modelPtr, thumbnailView](const QModelIndex &modelIndex,
                                                                    const QPoint &globalPos) {
        if (!modelPtr || !modelIndex.isValid()) {
            return;
        }

        const QString imagePath = modelPtr->imagePathAt(modelIndex.row());
        if (imagePath.isEmpty()) {
            return;
        }

        ImageContextMenu::showMenu(thumbnailView, globalPos, imagePath);
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
    }

    updateGlobalScanStatus();
    emitSelectionChanged();
}

void BrowsePanel::onSessionCleared()
{
    stopInterleavedLoading();
    clearAllColumns();
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

    for (int c = 0; c < m_columns.size(); ++c) {
        auto &col = m_columns[c];
        QList<int> selected = col.model->selectedIndices();
        if (selected.isEmpty()) {
            continue;
        }

        const int currentIdx = selected.first();
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

    if (anyChanged) {
        requestVisibleThumbnailsForAllColumns();
        emitSelectionChanged();
    }
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
        col.progressLabel->setText(tr("Discovered: %1 (done)").arg(col.discoveredCount));
    } else {
        col.progressLabel->setText(tr("Discovered: %1 (scanning...)").arg(col.discoveredCount));
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
