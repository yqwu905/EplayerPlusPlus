#include "BrowsePanel.h"
#include "ThumbnailWidget.h"
#include "models/CompareSession.h"
#include "models/ImageListModel.h"
#include "services/ImageLoader.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QDir>
#include <QTimer>

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

BrowsePanel::~BrowsePanel() = default;

void BrowsePanel::setupUi()
{
    m_columnsLayout = new QHBoxLayout(this);
    m_columnsLayout->setContentsMargins(8, 8, 8, 8);
    m_columnsLayout->setSpacing(8);

    // Add stretch so columns are left-aligned when fewer than 4
    m_columnsLayout->addStretch();

    setStyleSheet("BrowsePanel { background-color: #F5F5F5; }");
}

void BrowsePanel::onFolderAdded(const QString &folderPath, int index)
{
    Q_UNUSED(index);

    ColumnInfo col;

    // Create the model (setFolder is now async)
    col.model = new ImageListModel(this);
    col.model->setImageLoader(m_imageLoader);

    // Create scroll area
    col.scrollArea = new QScrollArea(this);
    col.scrollArea->setWidgetResizable(true);
    col.scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    col.scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    col.scrollArea->setMinimumWidth(210);
    col.scrollArea->setStyleSheet(
        "QScrollArea { background-color: #F5F5F5; border: none; border-radius: 8px; }");

    // Create container widget inside scroll area
    col.container = new QWidget();
    col.container->setStyleSheet("QWidget { background-color: #F5F5F5; }");
    col.containerLayout = new QVBoxLayout(col.container);
    col.containerLayout->setContentsMargins(8, 8, 8, 8);
    col.containerLayout->setSpacing(8);

    // Header with folder name and close button — Fluent 2 card style
    auto *headerWidget = new QWidget(col.container);
    auto *headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(12, 8, 8, 8);
    headerLayout->setSpacing(8);

    QString displayName = QDir(folderPath).dirName();
    auto *headerLabel = new QLabel(displayName, headerWidget);
    headerLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    QFont headerFont = headerLabel->font();
    headerFont.setWeight(QFont::DemiBold);
    headerFont.setPointSize(12);
    headerLabel->setFont(headerFont);
    headerLabel->setStyleSheet(
        "QLabel { color: #1A1A1A; background: transparent; border: none; }");
    headerLayout->addWidget(headerLabel, 1);

    auto *closeBtn = new QPushButton(QStringLiteral("\u00D7"), headerWidget);
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

    connect(closeBtn, &QPushButton::clicked, this, [this, folderPath]() {
        m_session->removeFolder(folderPath);
    });

    headerWidget->setStyleSheet(
        "QWidget { background-color: #FFFFFF; border-radius: 8px; }");
    col.containerLayout->addWidget(headerWidget);

    // Loading label — shown while async scan is in progress
    col.loadingLabel = new QLabel(tr("Scanning..."), col.container);
    col.loadingLabel->setAlignment(Qt::AlignCenter);
    col.loadingLabel->setStyleSheet(
        "QLabel { color: #9E9E9E; padding: 20px; background: transparent; border: none; }");
    col.containerLayout->addWidget(col.loadingLabel);

    col.containerLayout->addStretch();
    col.scrollArea->setWidget(col.container);

    // Insert before the stretch at the end
    int insertPos = m_columnsLayout->count() - 1; // before the trailing stretch
    m_columnsLayout->insertWidget(insertPos, col.scrollArea, 1);

    m_columns.append(col);
    int colIdx = m_columns.size() - 1;

    // Connect folderReady signal to build thumbnails when scan completes
    connect(col.model, &ImageListModel::folderReady,
            this, [this, colIdx]() {
        onFolderReady(colIdx);
    });

    // Connect scroll to lazy-load more thumbnails
    connect(col.scrollArea->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this, colIdx](int /*value*/) {
        if (colIdx < 0 || colIdx >= m_columns.size()) return;
        const auto &c = m_columns[colIdx];
        if (!c.scrollArea || !c.model) return;

        // Estimate visible range from scroll position
        int scrollY = c.scrollArea->verticalScrollBar()->value();
        int viewportH = c.scrollArea->viewport()->height();
        int itemH = 220; // approximate thumbnail widget height
        int firstVisible = qMax(0, scrollY / itemH - 2);
        int lastVisible = qMin(c.model->imageCount() - 1,
                               (scrollY + viewportH) / itemH + 2);
        c.model->loadThumbnailsForRange(firstVisible, lastVisible);
    });

    // Connect model thumbnail updates to widget updates
    connect(col.model, &QAbstractItemModel::dataChanged,
            this, [this, colIdx](
                const QModelIndex &topLeft, const QModelIndex &bottomRight,
                const QList<int> &roles) {
        if (colIdx < 0 || colIdx >= m_columns.size()) return;
        if (!roles.contains(Qt::DecorationRole) &&
            !roles.contains(ImageListModel::ThumbnailRole)) {
            return;
        }
        const auto &c = m_columns[colIdx];
        for (int row = topLeft.row(); row <= bottomRight.row(); ++row) {
            if (row >= 0 && row < c.thumbnailWidgets.size()) {
                QVariant thumbVar = c.model->data(c.model->index(row),
                                                   ImageListModel::ThumbnailRole);
                if (thumbVar.canConvert<QImage>()) {
                    c.thumbnailWidgets[row]->setThumbnail(thumbVar.value<QImage>());
                }
            }
        }
    });

    // Start async folder scan
    col.model->setFolder(folderPath);
}

void BrowsePanel::onFolderReady(int columnIndex)
{
    if (columnIndex < 0 || columnIndex >= m_columns.size()) return;

    auto &col = m_columns[columnIndex];

    // Remove loading label
    if (col.loadingLabel) {
        col.containerLayout->removeWidget(col.loadingLabel);
        delete col.loadingLabel;
        col.loadingLabel = nullptr;
    }

    col.builtCount = 0;

    // Start building thumbnails in batches
    buildThumbnailsBatch(columnIndex);
}

void BrowsePanel::buildThumbnailsBatch(int columnIndex)
{
    if (columnIndex < 0 || columnIndex >= m_columns.size()) return;

    auto &col = m_columns[columnIndex];
    if (!col.model) return;

    int totalImages = col.model->imageCount();
    int end = qMin(col.builtCount + kBatchSize, totalImages);

    // Remove the trailing stretch before adding widgets
    if (col.builtCount == 0 && col.containerLayout->count() > 0) {
        QLayoutItem *lastItem = col.containerLayout->itemAt(col.containerLayout->count() - 1);
        if (lastItem && lastItem->spacerItem()) {
            col.containerLayout->removeItem(lastItem);
            delete lastItem;
        }
    }

    for (int i = col.builtCount; i < end; ++i) {
        auto *thumb = new ThumbnailWidget(col.container);
        thumb->setFilePath(col.model->imagePathAt(i));
        thumb->setFileName(col.model->fileNameAt(i));

        connect(thumb, &ThumbnailWidget::clicked,
                this, &BrowsePanel::onThumbnailClicked);

        col.containerLayout->addWidget(thumb);
        col.thumbnailWidgets.append(thumb);
    }

    col.builtCount = end;

    // Request thumbnails for this batch
    if (end > 0) {
        int batchStart = end - qMin(kBatchSize, end);
        col.model->loadThumbnailsForRange(batchStart, end - 1);
    }

    if (col.builtCount < totalImages) {
        // Schedule next batch
        QTimer::singleShot(0, this, [this, columnIndex]() {
            buildThumbnailsBatch(columnIndex);
        });
    } else {
        // All widgets built — add trailing stretch
        col.containerLayout->addStretch();
    }
}

void BrowsePanel::onFolderRemoved(const QString &folderPath, int index)
{
    Q_UNUSED(folderPath);

    if (index < 0 || index >= m_columns.size()) {
        return;
    }

    ColumnInfo &col = m_columns[index];
    m_columnsLayout->removeWidget(col.scrollArea);
    delete col.scrollArea;
    delete col.model;

    m_columns.removeAt(index);
    emitSelectionChanged();
}

void BrowsePanel::onSessionCleared()
{
    clearAllColumns();
    emitSelectionChanged();
}

void BrowsePanel::onThumbnailClicked(const QString &filePath,
                                      Qt::KeyboardModifiers modifiers)
{
    int clickedCol = findColumn(filePath);
    if (clickedCol < 0) return;

    int clickedIdx = findIndexInColumn(clickedCol, filePath);
    if (clickedIdx < 0) return;

    if (modifiers & Qt::ControlModifier) {
        // Ctrl+Click: select this image + same-filename images in all other columns
        clearSelection();
        QString fileName = m_columns[clickedCol].model->fileNameAt(clickedIdx);

        for (int c = 0; c < m_columns.size(); ++c) {
            int matchIdx = m_columns[c].model->indexOfFileName(fileName);
            if (matchIdx >= 0) {
                m_columns[c].model->setSelected(matchIdx, true);
                if (matchIdx < m_columns[c].thumbnailWidgets.size()) {
                    m_columns[c].thumbnailWidgets[matchIdx]->setSelected(true);
                }
            }
        }
    } else if (modifiers & Qt::AltModifier) {
        // Alt+Click: select this image + same-index (order) images in all other columns
        clearSelection();
        for (int c = 0; c < m_columns.size(); ++c) {
            if (clickedIdx < m_columns[c].model->imageCount()) {
                m_columns[c].model->setSelected(clickedIdx, true);
                if (clickedIdx < m_columns[c].thumbnailWidgets.size()) {
                    m_columns[c].thumbnailWidgets[clickedIdx]->setSelected(true);
                }
            }
        }
    } else {
        // Plain click: select only this single image in its own column,
        // without affecting other columns' selections
        clearColumnSelection(clickedCol);
        m_columns[clickedCol].model->setSelected(clickedIdx, true);
        if (clickedIdx < m_columns[clickedCol].thumbnailWidgets.size()) {
            m_columns[clickedCol].thumbnailWidgets[clickedIdx]->setSelected(true);
        }
    }

    emitSelectionChanged();
}

void BrowsePanel::clearAllColumns()
{
    for (auto &col : m_columns) {
        m_columnsLayout->removeWidget(col.scrollArea);
        delete col.scrollArea;
        delete col.model;
    }
    m_columns.clear();
}

void BrowsePanel::clearSelection()
{
    for (auto &col : m_columns) {
        col.model->clearSelection();
        for (auto *thumb : col.thumbnailWidgets) {
            thumb->setSelected(false);
        }
    }
}

void BrowsePanel::clearColumnSelection(int column)
{
    if (column < 0 || column >= m_columns.size()) return;

    auto &col = m_columns[column];
    col.model->clearSelection();
    for (auto *thumb : col.thumbnailWidgets) {
        thumb->setSelected(false);
    }
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
}

int BrowsePanel::findColumn(const QString &filePath) const
{
    for (int c = 0; c < m_columns.size(); ++c) {
        for (int i = 0; i < m_columns[c].model->imageCount(); ++i) {
            if (m_columns[c].model->imagePathAt(i) == filePath) {
                return c;
            }
        }
    }
    return -1;
}

int BrowsePanel::findIndexInColumn(int column, const QString &filePath) const
{
    if (column < 0 || column >= m_columns.size()) {
        return -1;
    }
    for (int i = 0; i < m_columns[column].model->imageCount(); ++i) {
        if (m_columns[column].model->imagePathAt(i) == filePath) {
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
        if (selected.isEmpty()) continue;

        // Use the first selected index as the anchor
        int currentIdx = selected.first();
        int newIdx = currentIdx + delta;

        if (newIdx < 0 || newIdx >= col.model->imageCount()) continue;

        // Clear old selection in this column
        col.model->clearSelection();
        for (auto *thumb : col.thumbnailWidgets) {
            thumb->setSelected(false);
        }

        // Select new index
        col.model->setSelected(newIdx, true);
        if (newIdx < col.thumbnailWidgets.size()) {
            col.thumbnailWidgets[newIdx]->setSelected(true);

            // Scroll to make the selected thumbnail visible
            if (col.scrollArea) {
                col.scrollArea->ensureWidgetVisible(col.thumbnailWidgets[newIdx], 50, 50);
            }
        }

        anyChanged = true;
    }

    if (anyChanged) {
        emitSelectionChanged();
    }
}
