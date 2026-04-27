#include "BrowsePanel.h"
#include "ThumbnailWidget.h"
#include "models/CompareSession.h"
#include "models/ImageListModel.h"
#include "services/ImageLoader.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <QCheckBox>
#include <QPushButton>
#include <QScrollBar>
#include <QDir>
#include <QFontMetrics>
#include <QTimer>
#include <limits>
#include <vector>

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

    // Add stretch so columns are left-aligned when fewer than max compare count
    m_columnsLayout->addStretch();
    m_rootLayout->addLayout(m_columnsLayout);

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
    
    // Create an empty label first to set font
    auto *headerLabel = new QLabel(headerWidget);
    headerLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    QFont headerFont = headerLabel->font();
    headerFont.setWeight(QFont::DemiBold);
    headerFont.setPointSize(12);
    headerLabel->setFont(headerFont);
    headerLabel->setStyleSheet(
        "QLabel { color: #1A1A1A; background: transparent; border: none; }");
        
    // Elide text to avoid pushing the close button out of view
    // Available width is approx: 220 (Thumbnail width) - 28 (Close btn) - 44 (Margins/Spacing) ~= 148px
    QFontMetrics fm(headerFont);
    QString elidedName = fm.elidedText(displayName, Qt::ElideRight, 140);
    headerLabel->setText(elidedName);
    headerLabel->setToolTip(displayName);
    
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

    QScrollArea *scrollAreaPtr = col.scrollArea;
    connect(closeBtn, &QPushButton::clicked, this, [this, scrollAreaPtr]() {
        // Find current index dynamically — indices shift after removals
        for (int i = 0; i < m_columns.size(); ++i) {
            if (m_columns[i].scrollArea == scrollAreaPtr) {
                m_session->removeFolderAt(i);
                return;
            }
        }
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

    col.progressLabel = new QLabel(tr("Discovered: 0"), col.container);
    col.progressLabel->setAlignment(Qt::AlignCenter);
    col.progressLabel->setStyleSheet(
        "QLabel { color: #7D7D7D; padding: 4px; background: transparent; border: none; }");
    col.containerLayout->addWidget(col.progressLabel);

    col.containerLayout->addStretch();
    col.scrollArea->setWidget(col.container);

    // Insert before the stretch at the end
    int insertPos = m_columnsLayout->count() - 1; // before the trailing stretch
    m_columnsLayout->insertWidget(insertPos, col.scrollArea, 1);

    m_columns.append(col);
    int colIdx = m_columns.size() - 1;

    // Connect folderReady signal for scan/progress state.
    connect(col.model, &ImageListModel::folderReady,
            this, [this, colIdx]() {
        onFolderReady(colIdx);
    });
    connect(col.model, &QAbstractItemModel::rowsInserted,
            this, [this, colIdx](const QModelIndex &parent, int first, int last) {
        if (!parent.isValid()) {
            onModelRowsInserted(colIdx, first, last);
        }
    });
    connect(col.model, &ImageListModel::scanProgressChanged,
            this, [this, colIdx](int discoveredCount, bool finished) {
        if (colIdx < 0 || colIdx >= m_columns.size()) {
            return;
        }
        auto &column = m_columns[colIdx];
        column.discoveredCount = discoveredCount;
        column.scanFinished = finished;
        updateColumnProgressLabel(colIdx);
        updateGlobalScanStatus();
    });

    // Connect scroll to lazy-load more thumbnails
    connect(col.scrollArea->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this, colIdx](int /*value*/) {
        if (colIdx < 0 || colIdx >= m_columns.size()) return;
        const auto &c = m_columns[colIdx];
        if (!c.scrollArea || !c.model) return;

        auto [firstVisible, lastVisible] = visibleRangeForColumn(c);
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

    // Start async folder scan (one worker task per folder).
    col.model->setFolder(folderPath);
    updateGlobalScanStatus();
}

void BrowsePanel::onFolderReady(int columnIndex)
{
    if (columnIndex < 0 || columnIndex >= m_columns.size()) return;

    auto &col = m_columns[columnIndex];

    col.scanFinished = true;
    col.discoveredCount = col.model ? col.model->imageCount() : col.discoveredCount;

    // Remove loading label
    if (col.loadingLabel) {
        col.containerLayout->removeWidget(col.loadingLabel);
        delete col.loadingLabel;
        col.loadingLabel = nullptr;
    }
    updateColumnProgressLabel(columnIndex);
    updateGlobalScanStatus();

    // Load visible thumbnails first for instant feedback
    requestVisibleThumbnailsForAllColumns();
    requestSelectedPriorityThumbnails();

    // Start or continue interleaved thumbnail loading across all columns
    startInterleavedLoading();
}

void BrowsePanel::onModelRowsInserted(int columnIndex, int first, int last)
{
    buildThumbnailPlaceholders(columnIndex, first, last);
    requestVisibleThumbnailsForAllColumns();
    requestSelectedPriorityThumbnails();
    startInterleavedLoading();
}

void BrowsePanel::buildThumbnailPlaceholders(int columnIndex, int first, int last)
{
    if (columnIndex < 0 || columnIndex >= m_columns.size()) return;

    auto &col = m_columns[columnIndex];
    if (!col.model) return;

    if (first < 0 || last < first) {
        return;
    }

    // Remove the trailing stretch before adding widgets
    if (col.thumbnailWidgets.isEmpty() && col.containerLayout->count() > 0) {
        QLayoutItem *lastItem = col.containerLayout->itemAt(col.containerLayout->count() - 1);
        if (lastItem && lastItem->spacerItem()) {
            col.containerLayout->removeItem(lastItem);
            delete lastItem;
        }
    }

    int insertIndex = qBound(0, first, col.thumbnailWidgets.size());
    for (int i = first; i <= last; ++i) {
        auto *thumb = new ThumbnailWidget(col.container);
        thumb->setFilePath(col.model->imagePathAt(i));
        thumb->setFileName(col.model->fileNameAt(i));

        connect(thumb, &ThumbnailWidget::clicked,
                this, &BrowsePanel::onThumbnailClicked);

        col.containerLayout->insertWidget(insertIndex, thumb);
        col.thumbnailWidgets.insert(insertIndex, thumb);
        ++insertIndex;
    }

    col.builtCount = col.thumbnailWidgets.size();
    if (col.containerLayout->count() == 0 ||
        !col.containerLayout->itemAt(col.containerLayout->count() - 1)->spacerItem()) {
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

    // Stop interleaved loading if no columns remain
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

void BrowsePanel::onThumbnailClicked(const QString &filePath,
                                      Qt::KeyboardModifiers modifiers)
{
    Q_UNUSED(filePath);

    const auto *clickedThumb = qobject_cast<const ThumbnailWidget *>(sender());
    int clickedCol = -1;
    int clickedIdx = -1;
    if (!clickedThumb || !findThumbnailPosition(clickedThumb, clickedCol, clickedIdx)) {
        return;
    }

    if (modifiers & Qt::ControlModifier) {
        // Ctrl+Click: select this image + same-index (order) images in all other columns
        clearSelection();
        QList<int> matchedIndices(m_columns.size(), -1);
        for (int c = 0; c < m_columns.size(); ++c) {
            if (clickedIdx < m_columns[c].model->imageCount()) {
                matchedIndices[c] = clickedIdx;
                m_columns[c].model->setSelected(clickedIdx, true);
                if (clickedIdx < m_columns[c].thumbnailWidgets.size()) {
                    m_columns[c].thumbnailWidgets[clickedIdx]->setSelected(true);
                }
            }
        }

        alignColumnsToAnchor(clickedCol, clickedIdx, matchedIndices);
    } else if (modifiers & Qt::AltModifier) {
        // Alt+Click: select this image + filename-matched images in all other columns
        clearSelection();
        const QString fileName = m_columns[clickedCol].model->fileNameAt(clickedIdx);
        QList<int> matchedIndices(m_columns.size(), -1);

        for (int c = 0; c < m_columns.size(); ++c) {
            const int matchIdx = findFileNameMatchIndex(c, fileName);
            if (matchIdx >= 0) {
                matchedIndices[c] = matchIdx;
                m_columns[c].model->setSelected(matchIdx, true);
                if (matchIdx < m_columns[c].thumbnailWidgets.size()) {
                    m_columns[c].thumbnailWidgets[matchIdx]->setSelected(true);
                }
            }
        }
        alignColumnsToAnchor(clickedCol, clickedIdx, matchedIndices);
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

void BrowsePanel::alignColumnsToAnchor(int anchorColumn,
                                       int anchorIndex,
                                       const QList<int> &matchedIndices)
{
    if (anchorColumn < 0 || anchorColumn >= m_columns.size()) {
        return;
    }

    const auto &anchorCol = m_columns[anchorColumn];
    if (!anchorCol.scrollArea || anchorIndex < 0 ||
        anchorIndex >= anchorCol.thumbnailWidgets.size()) {
        return;
    }

    auto *anchorScrollBar = anchorCol.scrollArea->verticalScrollBar();
    const int anchorYInViewport =
        anchorCol.thumbnailWidgets[anchorIndex]->y() - anchorScrollBar->value();

    for (int c = 0; c < m_columns.size(); ++c) {
        if (c == anchorColumn || c >= matchedIndices.size()) {
            continue;
        }

        const int targetIndex = matchedIndices[c];
        auto &targetCol = m_columns[c];
        if (!targetCol.scrollArea || targetIndex < 0 ||
            targetIndex >= targetCol.thumbnailWidgets.size()) {
            continue;
        }

        auto *targetScrollBar = targetCol.scrollArea->verticalScrollBar();
        const int targetY = targetCol.thumbnailWidgets[targetIndex]->y();
        const int desiredScroll = qBound(targetScrollBar->minimum(),
                                         targetY - anchorYInViewport,
                                         targetScrollBar->maximum());
        targetScrollBar->setValue(desiredScroll);
    }
}

void BrowsePanel::clearAllColumns()
{
    for (auto &col : m_columns) {
        m_columnsLayout->removeWidget(col.scrollArea);
        delete col.scrollArea;
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
    requestSelectedPriorityThumbnails();
    preloadNeighborImagesForSelection();
    startInterleavedLoading();
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

bool BrowsePanel::findThumbnailPosition(const ThumbnailWidget *thumbnail,
                                        int &column,
                                        int &indexInColumn) const
{
    if (!thumbnail) {
        return false;
    }

    for (int c = 0; c < m_columns.size(); ++c) {
        const int idx = m_columns[c].thumbnailWidgets.indexOf(
            const_cast<ThumbnailWidget *>(thumbnail));
        if (idx >= 0) {
            column = c;
            indexInColumn = idx;
            return true;
        }
    }

    return false;
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

void BrowsePanel::startInterleavedLoading()
{
    if (m_interleavedLoadTimer && m_interleavedLoadTimer->isActive()) {
        return; // Already running
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
    bool anyMoreToLoad = false;
    requestVisibleThumbnailsForAllColumns();
    requestSelectedPriorityThumbnails();

    for (int c = 0; c < m_columns.size(); ++c) {
        auto &col = m_columns[c];
        if (col.model && col.model->hasMoreToLoad()) {
            col.model->loadNextThumbnailBatch(kThumbnailBatchPerTick);
            anyMoreToLoad = true;
        }
    }

    if (m_imageLoader) {
        m_imageLoader->cancelThumbnailRequestsExcept(aggregateVisiblePaths());
    }

    if (!anyMoreToLoad) {
        stopInterleavedLoading();
    }
}

QPair<int, int> BrowsePanel::visibleRangeForColumn(const ColumnInfo &column) const
{
    if (!column.scrollArea || !column.model || column.model->imageCount() <= 0) {
        return {0, -1};
    }

    const int scrollY = column.scrollArea->verticalScrollBar()->value();
    const int viewportH = column.scrollArea->viewport()->height();
    const int itemH = 220;
    const int firstVisible = qMax(0, scrollY / itemH - 2);
    const int lastVisible = qMin(column.model->imageCount() - 1,
                                 (scrollY + viewportH) / itemH + 3);
    return {firstVisible, lastVisible};
}

void BrowsePanel::requestVisibleThumbnailsForAllColumns()
{
    for (auto &col : m_columns) {
        if (!col.model || !col.scrollArea || col.model->imageCount() <= 0) {
            continue;
        }
        auto [firstVisible, lastVisible] = visibleRangeForColumn(col);
        if (lastVisible >= firstVisible) {
            col.model->loadThumbnailsForRange(firstVisible, lastVisible);
        }
    }
}

void BrowsePanel::requestSelectedPriorityThumbnails()
{
    for (auto &col : m_columns) {
        if (!col.model || col.model->imageCount() <= 0) {
            continue;
        }

        const QList<int> selectedIndices = col.model->selectedIndices();
        for (int selectedIndex : selectedIndices) {
            const int first = qMax(0, selectedIndex - 3);
            const int last = qMin(col.model->imageCount() - 1, selectedIndex + 3);
            col.model->loadThumbnailsForRange(first, last);
        }
    }
}

QSet<QString> BrowsePanel::aggregateVisiblePaths() const
{
    QSet<QString> visiblePaths;
    for (const auto &col : m_columns) {
        if (!col.model || !col.scrollArea || col.model->imageCount() <= 0) {
            continue;
        }
        auto [firstVisible, lastVisible] = visibleRangeForColumn(col);
        for (int i = firstVisible; i <= lastVisible; ++i) {
            QString path = col.model->imagePathAt(i);
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
