#include "ComparePanel.h"
#include "ArrowOverlay.h"
#include "ZoomableImageWidget.h"
#include "services/ImageComparer.h"
#include "services/SettingsManager.h"
#include "models/CompareSession.h"

#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QToolBar>
#include <QAction>
#include <QScrollArea>
#include <QDir>
#include <QFileInfo>
#include <QEvent>
#include <QKeyEvent>
#include <QApplication>

ComparePanel::ComparePanel(CompareSession *session,
                           SettingsManager *settingsManager,
                           QWidget *parent)
    : QWidget(parent)
    , m_session(session)
    , m_settingsManager(settingsManager)
{
    if (m_settingsManager) {
        m_threshold = m_settingsManager->comparisonThreshold();
    }
    setupUi();

    // Make the panel focusable for keyboard navigation
    setFocusPolicy(Qt::StrongFocus);

    connect(m_session, &CompareSession::folderAdded,
            this, &ComparePanel::onFolderAdded);
    connect(m_session, &CompareSession::folderRemoved,
            this, &ComparePanel::onFolderRemoved);
    connect(m_session, &CompareSession::cleared,
            this, &ComparePanel::onSessionCleared);
}

ComparePanel::~ComparePanel() = default;

void ComparePanel::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ---- Toolbar — Fluent 2 style ----
    m_toolBar = new QToolBar(this);
    m_toolBar->setIconSize(QSize(16, 16));
    m_toolBar->setStyleSheet(
        "QToolBar { background-color: #FAFAFA; border: none; "
        "border-bottom: 1px solid #E0E0E0; padding: 4px 12px; spacing: 4px; }");

    // Navigation buttons — Fluent 2 style
    m_prevAction = m_toolBar->addAction(QStringLiteral("\u25B2"));  // ▲
    m_prevAction->setToolTip(tr("Previous image (Up arrow key)"));
    connect(m_prevAction, &QAction::triggered, this, [this]() {
        emit navigatePreviousRequested();
    });

    m_nextAction = m_toolBar->addAction(QStringLiteral("\u25BC"));  // ▼
    m_nextAction->setToolTip(tr("Next image (Down arrow key)"));
    connect(m_nextAction, &QAction::triggered, this, [this]() {
        emit navigateNextRequested();
    });

    m_toolBar->addSeparator();

    // Mode toggle button — Fluent 2 pill / toggle style
    m_modeAction = m_toolBar->addAction(tr("Mode: Swap"));
    m_modeAction->setToolTip(tr("Click to switch between Swap and Tolerance mode"));
    m_modeAction->setCheckable(true);
    connect(m_modeAction, &QAction::triggered, this, &ComparePanel::onModeToggled);

    m_toolBar->addSeparator();

    // Threshold controls (only visible in Tolerance mode)
    m_thresholdContainer = new QWidget(m_toolBar);
    auto *thresholdLayout = new QHBoxLayout(m_thresholdContainer);
    thresholdLayout->setContentsMargins(0, 0, 0, 0);
    thresholdLayout->setSpacing(8);

    auto *thresholdLabel = new QLabel(tr("Threshold"), m_thresholdContainer);
    thresholdLabel->setStyleSheet(
        "QLabel { color: #616161; font-size: 12px; background: transparent; }");
    thresholdLayout->addWidget(thresholdLabel);

    m_thresholdSlider = new QSlider(Qt::Horizontal, m_thresholdContainer);
    m_thresholdSlider->setRange(0, 255);
    m_thresholdSlider->setValue(m_threshold);
    m_thresholdSlider->setFixedWidth(160);
    m_thresholdSlider->setToolTip(tr("Tolerance map threshold (0-255)"));
    thresholdLayout->addWidget(m_thresholdSlider);

    m_thresholdValueLabel = new QLabel(QString("%1").arg(m_threshold), m_thresholdContainer);
    m_thresholdValueLabel->setMinimumWidth(30);
    m_thresholdValueLabel->setStyleSheet(
        "QLabel { color: #1A1A1A; font-size: 12px; font-weight: 600; "
        "background: transparent; }");
    thresholdLayout->addWidget(m_thresholdValueLabel);

    m_toolBar->addWidget(m_thresholdContainer);
    m_thresholdContainer->setVisible(false); // Hidden by default (Swap mode)

    connect(m_thresholdSlider, &QSlider::valueChanged,
            this, &ComparePanel::onThresholdChanged);

    mainLayout->addWidget(m_toolBar);

    // ---- Scroll area containing the grid ----
    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet(
        "QScrollArea { background-color: #F5F5F5; border: none; }");

    m_gridContainer = new QWidget(scrollArea);
    m_gridContainer->setStyleSheet("QWidget { background-color: #F5F5F5; }");
    m_gridLayout = new QGridLayout(m_gridContainer);
    m_gridLayout->setContentsMargins(12, 12, 12, 12);
    m_gridLayout->setSpacing(12);

    scrollArea->setWidget(m_gridContainer);
    mainLayout->addWidget(scrollArea);
}

// ---- Mode toggle ----

void ComparePanel::onModeToggled()
{
    if (m_compareMode == SwapMode) {
        m_compareMode = ToleranceMode;
        m_modeAction->setText(tr("Mode: Tolerance"));
        m_modeAction->setChecked(true);
        m_thresholdContainer->setVisible(true);
    } else {
        m_compareMode = SwapMode;
        m_modeAction->setText(tr("Mode: Swap"));
        m_modeAction->setChecked(false);
        m_thresholdContainer->setVisible(false);

        // Restore all images showing tolerance maps back to original
        for (int i = 0; i < m_cells.size(); ++i) {
            if (m_cells[i].showingToleranceMap) {
                showOriginalImage(i);
            }
        }
    }
}

// ---- Session change handlers ----

void ComparePanel::onFolderAdded(const QString &folderPath, int /*index*/)
{
    ImageCell cell = createCell(folderPath);
    m_cells.append(cell);
    rebuildGrid();
}

void ComparePanel::onFolderRemoved(const QString &folderPath, int /*index*/)
{
    for (int i = 0; i < m_cells.size(); ++i) {
        if (m_cells[i].folderPath == folderPath) {
            if (m_cells[i].imageContainer) {
                m_cells[i].imageContainer->removeEventFilter(this);
            }
            m_gridLayout->removeWidget(m_cells[i].container);
            delete m_cells[i].container;
            m_cells.removeAt(i);
            break;
        }
    }
    rebuildGrid();
}

void ComparePanel::onSessionCleared()
{
    clearCells();
}

// ---- Selection update ----

void ComparePanel::setSelectedImages(const QList<QPair<QString, QString>> &selectedImages)
{
    QHash<QString, QString> selectionMap;
    for (const auto &pair : selectedImages) {
        selectionMap.insert(pair.first, pair.second);
    }

    for (int i = 0; i < m_cells.size(); ++i) {
        auto it = selectionMap.find(m_cells[i].folderPath);
        if (it != selectionMap.end()) {
            QString newImagePath = it.value();
            if (newImagePath != m_cells[i].imagePath) {
                m_cells[i].imagePath = newImagePath;
                m_cells[i].showingToleranceMap = false;
                m_cells[i].toleranceSourceIndex = -1;
                loadImage(i);

                QString folderName = QDir(m_cells[i].folderPath).dirName();
                QString fileName = QFileInfo(newImagePath).fileName();
                m_cells[i].headerLabel->setText(
                    QStringLiteral("<b>%1</b><br>%2").arg(folderName, fileName));
            }
        }
    }
}

void ComparePanel::clear()
{
    clearCells();
}

bool ComparePanel::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Resize) {
        for (int i = 0; i < m_cells.size(); ++i) {
            if (m_cells[i].imageContainer == watched) {
                resizeImageCell(i);
                return false;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ComparePanel::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Up) {
        emit navigatePreviousRequested();
        event->accept();
        return;
    } else if (event->key() == Qt::Key_Down) {
        emit navigateNextRequested();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

// ---- Cell management ----

ComparePanel::ImageCell ComparePanel::createCell(const QString &folderPath)
{
    ImageCell cell;
    cell.folderPath = folderPath;
    cell.hasImage = false;

    // ---- Fluent 2 card container ----
    cell.container = new QWidget(m_gridContainer);
    cell.container->setStyleSheet(
        "QWidget#compareCellContainer { background-color: #FFFFFF; "
        "border: 1px solid #E0E0E0; border-radius: 8px; }");
    cell.container->setObjectName("compareCellContainer");
    auto *cellLayout = new QVBoxLayout(cell.container);
    cellLayout->setContentsMargins(0, 0, 0, 0);
    cellLayout->setSpacing(0);

    // ---- Header — Fluent 2 style ----
    QString folderName = QDir(folderPath).dirName();
    cell.headerLabel = new QLabel(
        QStringLiteral("<b>%1</b><br><span style='color:#9E9E9E;'>No image selected</span>").arg(folderName),
        cell.container);
    cell.headerLabel->setAlignment(Qt::AlignCenter);
    cell.headerLabel->setStyleSheet(
        "QLabel { background-color: #FFFFFF; color: #1A1A1A; "
        "padding: 8px 12px; border: none; "
        "border-top-left-radius: 8px; border-top-right-radius: 8px; "
        "border-bottom: 1px solid #E0E0E0; font-size: 12px; }");
    cellLayout->addWidget(cell.headerLabel);

    // ---- Image container ----
    cell.imageContainer = new QWidget(cell.container);
    cell.imageContainer->setMinimumSize(200, 200);
    cell.imageContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    cell.imageContainer->setStyleSheet("QWidget { background-color: #F5F5F5; }");

    cell.imageWidget = new ZoomableImageWidget(cell.imageContainer);
    cell.imageWidget->setText(tr("Click a thumbnail\nto compare"));

    // ArrowOverlay is a child of imageWidget so that ignored mouse/wheel
    // events propagate from ArrowOverlay -> ZoomableImageWidget
    cell.arrowOverlay = new ArrowOverlay(cell.imageWidget);

    cellLayout->addWidget(cell.imageContainer, 1);
    cell.imageContainer->installEventFilter(this);

    // Connect zoom/pan signals for linked view
    connect(cell.imageWidget, &ZoomableImageWidget::zoomChanged,
            this, &ComparePanel::onCellZoomChanged);
    connect(cell.imageWidget, &ZoomableImageWidget::panChanged,
            this, &ComparePanel::onCellPanChanged);
    connect(cell.imageWidget, &ZoomableImageWidget::viewReset,
            this, &ComparePanel::onCellViewReset);

    return cell;
}

void ComparePanel::clearCells()
{
    for (auto &cell : m_cells) {
        if (cell.imageContainer) {
            cell.imageContainer->removeEventFilter(this);
        }
        if (cell.container) {
            m_gridLayout->removeWidget(cell.container);
            delete cell.container;
        }
    }
    m_cells.clear();
}

void ComparePanel::rebuildGrid()
{
    while (m_gridLayout->count() > 0) {
        m_gridLayout->takeAt(0);
    }

    int count = m_cells.size();
    if (count == 0) return;

    int cols = (count <= 1) ? 1 : 2;
    int rows = (count + cols - 1) / cols;

    for (int i = 0; i < count; ++i) {
        int row = i / cols;
        int col = i % cols;
        m_gridLayout->addWidget(m_cells[i].container, row, col);
    }

    for (int r = 0; r < rows; ++r) {
        m_gridLayout->setRowStretch(r, 1);
    }
    for (int c = 0; c < cols; ++c) {
        m_gridLayout->setColumnStretch(c, 1);
    }

    reconnectArrows();
}

void ComparePanel::reconnectArrows()
{
    for (int i = 0; i < m_cells.size(); ++i) {
        disconnect(m_cells[i].arrowOverlay, nullptr, this, nullptr);
        setupArrowsForCell(i);
    }
}

void ComparePanel::setupArrowsForCell(int cellIndex)
{
    if (cellIndex < 0 || cellIndex >= m_cells.size()) return;

    int count = m_cells.size();
    int cols = (count <= 1) ? 1 : 2;

    ImageCell &cell = m_cells[cellIndex];
    int row = cellIndex / cols;
    int col = cellIndex % cols;

    QList<ArrowOverlay::Direction> directions;

    if (col > 0) {
        int target = row * cols + (col - 1);
        if (target < count) {
            directions << ArrowOverlay::Left;
            cell.arrowOverlay->setTargetIndex(ArrowOverlay::Left, target);
        }
    }
    if (col < cols - 1) {
        int target = row * cols + (col + 1);
        if (target < count) {
            directions << ArrowOverlay::Right;
            cell.arrowOverlay->setTargetIndex(ArrowOverlay::Right, target);
        }
    }
    if (row > 0) {
        int target = (row - 1) * cols + col;
        if (target < count) {
            directions << ArrowOverlay::Up;
            cell.arrowOverlay->setTargetIndex(ArrowOverlay::Up, target);
        }
    }
    {
        int target = (row + 1) * cols + col;
        if (target < count) {
            directions << ArrowOverlay::Down;
            cell.arrowOverlay->setTargetIndex(ArrowOverlay::Down, target);
        }
    }

    cell.arrowOverlay->setDirections(directions);
    cell.arrowOverlay->setSourceIndex(cellIndex);

    connect(cell.arrowOverlay, &ArrowOverlay::arrowPressed,
            this, &ComparePanel::onArrowPressed);
    connect(cell.arrowOverlay, &ArrowOverlay::arrowReleased,
            this, &ComparePanel::onArrowReleased);
    connect(cell.arrowOverlay, &ArrowOverlay::arrowClicked,
            this, &ComparePanel::onArrowClicked);
}

void ComparePanel::loadImage(int cellIndex)
{
    if (cellIndex < 0 || cellIndex >= m_cells.size()) return;

    ImageCell &cell = m_cells[cellIndex];
    cell.originalImage = QImage(cell.imagePath);
    cell.hasImage = !cell.originalImage.isNull();
    cell.cachedToleranceImage = QImage();     // Invalidate tolerance cache

    // Update geometry
    QRect r = cell.imageContainer->rect();
    cell.imageWidget->setGeometry(r);
    // ArrowOverlay is child of imageWidget, geometry is relative to it
    cell.arrowOverlay->setGeometry(QRect(0, 0, r.width(), r.height()));
    cell.arrowOverlay->raise();

    showOriginalImage(cellIndex, true);  // resetView on initial load
}

void ComparePanel::clearImage(int cellIndex)
{
    if (cellIndex < 0 || cellIndex >= m_cells.size()) return;

    ImageCell &cell = m_cells[cellIndex];
    cell.imagePath.clear();
    cell.originalImage = QImage();
    cell.cachedToleranceImage = QImage();
    cell.hasImage = false;
    cell.showingToleranceMap = false;
    cell.toleranceSourceIndex = -1;
    cell.imageWidget->setText(tr("Click a thumbnail\nto compare"));

    QString folderName = QDir(cell.folderPath).dirName();
    cell.headerLabel->setText(
        QStringLiteral("<b>%1</b><br><i>No image selected</i>").arg(folderName));
}

void ComparePanel::resizeImageCell(int cellIndex)
{
    if (cellIndex < 0 || cellIndex >= m_cells.size()) return;

    ImageCell &cell = m_cells[cellIndex];
    if (!cell.imageContainer) return;

    QRect r = cell.imageContainer->rect();
    cell.imageWidget->setGeometry(r);
    // ArrowOverlay is child of imageWidget, geometry is relative to it
    cell.arrowOverlay->setGeometry(QRect(0, 0, r.width(), r.height()));
    cell.arrowOverlay->raise();

    // ZoomableImageWidget handles redraw internally on resize
}

void ComparePanel::showOriginalImage(int cellIndex, bool resetView)
{
    if (cellIndex < 0 || cellIndex >= m_cells.size()) return;

    ImageCell &cell = m_cells[cellIndex];
    if (cell.originalImage.isNull()) {
        cell.imageWidget->setText(tr("Failed to load image"));
        return;
    }

    cell.imageWidget->setImage(cell.originalImage, resetView);
    cell.showingToleranceMap = false;
    cell.toleranceSourceIndex = -1;
}

void ComparePanel::showToleranceMap(int sourceIndex, int targetIndex)
{
    if (sourceIndex < 0 || sourceIndex >= m_cells.size()) return;
    if (targetIndex < 0 || targetIndex >= m_cells.size()) return;

    const ImageCell &source = m_cells[sourceIndex];
    ImageCell &target = m_cells[targetIndex];

    if (source.originalImage.isNull() || target.originalImage.isNull()) return;

    // Only regenerate the tolerance image if source or threshold changed
    bool needRegenerate = target.cachedToleranceImage.isNull()
                          || target.toleranceSourceIndex != sourceIndex;

    if (needRegenerate) {
        target.cachedToleranceImage = ImageComparer::generateToleranceMap(
            source.originalImage, target.originalImage, m_threshold);
    }

    if (target.cachedToleranceImage.isNull()) return;

    target.imageWidget->setImage(target.cachedToleranceImage, false);
    target.showingToleranceMap = true;
    target.toleranceSourceIndex = sourceIndex;
}

void ComparePanel::showSourceOnTarget(int sourceIndex, int targetIndex)
{
    if (sourceIndex < 0 || sourceIndex >= m_cells.size()) return;
    if (targetIndex < 0 || targetIndex >= m_cells.size()) return;

    const ImageCell &source = m_cells[sourceIndex];

    if (source.originalImage.isNull()) return;

    m_cells[targetIndex].imageWidget->setImage(source.originalImage, false);
}

// ---- Arrow interaction (mode-dependent) ----

void ComparePanel::onArrowPressed(int sourceIndex, int targetIndex)
{
    if (m_compareMode == SwapMode) {
        showSourceOnTarget(sourceIndex, targetIndex);
    }
    // In ToleranceMode, press does nothing
}

void ComparePanel::onArrowReleased(int sourceIndex, int targetIndex)
{
    Q_UNUSED(sourceIndex);

    if (m_compareMode == SwapMode) {
        // Restore target to original
        if (targetIndex >= 0 && targetIndex < m_cells.size()) {
            showOriginalImage(targetIndex);
        }
    }
    // In ToleranceMode, release does nothing
}

void ComparePanel::onArrowClicked(int sourceIndex, int targetIndex)
{
    if (m_compareMode == ToleranceMode) {
        if (targetIndex < 0 || targetIndex >= m_cells.size()) return;

        ImageCell &target = m_cells[targetIndex];
        if (target.showingToleranceMap && target.toleranceSourceIndex == sourceIndex) {
            showOriginalImage(targetIndex);
        } else {
            showToleranceMap(sourceIndex, targetIndex);
        }
    }
    // In SwapMode, click does nothing
}

void ComparePanel::onThresholdChanged(int value)
{
    m_threshold = value;
    m_thresholdValueLabel->setText(QString("%1").arg(value));

    if (m_settingsManager) {
        m_settingsManager->setComparisonThreshold(value);
    }

    // Invalidate cached tolerance maps and regenerate
    for (int i = 0; i < m_cells.size(); ++i) {
        if (m_cells[i].showingToleranceMap && m_cells[i].toleranceSourceIndex >= 0) {
            m_cells[i].cachedToleranceImage = QImage(); // Force regeneration
            showToleranceMap(m_cells[i].toleranceSourceIndex, i);
        }
    }
}

// ---- Zoom/pan synchronization ----

int ComparePanel::findCellByWidget(QObject *widget) const
{
    for (int i = 0; i < m_cells.size(); ++i) {
        if (m_cells[i].imageWidget == widget) {
            return i;
        }
    }
    return -1;
}

void ComparePanel::onCellZoomChanged(double zoomLevel, QPointF focalPoint)
{
    if (m_syncingViews) return; // Prevent recursive sync
    m_syncingViews = true;

    int sourceIdx = findCellByWidget(sender());

    // Check if Ctrl is held — if so, only zoom this cell (independent mode)
    bool ctrlHeld = QApplication::keyboardModifiers() & Qt::ControlModifier;

    if (!ctrlHeld) {
        // Linked mode: sync all other cells
        for (int i = 0; i < m_cells.size(); ++i) {
            if (i != sourceIdx && m_cells[i].imageWidget) {
                m_cells[i].imageWidget->setZoomLevel(zoomLevel, focalPoint, false);
                // Also sync the pan offset from the source
                if (sourceIdx >= 0) {
                    QPointF srcPan = m_cells[sourceIdx].imageWidget->panOffset();
                    m_cells[i].imageWidget->setPanOffset(srcPan, false);
                }
            }
        }
    }

    m_syncingViews = false;
}

void ComparePanel::onCellPanChanged(QPointF offset)
{
    if (m_syncingViews) return;
    m_syncingViews = true;

    int sourceIdx = findCellByWidget(sender());

    bool ctrlHeld = QApplication::keyboardModifiers() & Qt::ControlModifier;

    if (!ctrlHeld) {
        for (int i = 0; i < m_cells.size(); ++i) {
            if (i != sourceIdx && m_cells[i].imageWidget) {
                m_cells[i].imageWidget->setPanOffset(offset, false);
            }
        }
    }

    m_syncingViews = false;
}

void ComparePanel::onCellViewReset()
{
    if (m_syncingViews) return;
    m_syncingViews = true;

    int sourceIdx = findCellByWidget(sender());

    bool ctrlHeld = QApplication::keyboardModifiers() & Qt::ControlModifier;

    if (!ctrlHeld) {
        for (int i = 0; i < m_cells.size(); ++i) {
            if (i != sourceIdx && m_cells[i].imageWidget) {
                m_cells[i].imageWidget->resetView(false);
            }
        }
    }

    m_syncingViews = false;
}
