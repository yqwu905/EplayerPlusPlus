#include "ComparePanel.h"
#include "ArrowOverlay.h"
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

    // ---- Toolbar ----
    m_toolBar = new QToolBar(this);
    m_toolBar->setIconSize(QSize(16, 16));

    // Mode toggle button
    m_modeAction = m_toolBar->addAction(tr("Mode: Swap"));
    m_modeAction->setToolTip(tr("Click to switch between Swap and Tolerance mode"));
    connect(m_modeAction, &QAction::triggered, this, &ComparePanel::onModeToggled);

    m_toolBar->addSeparator();

    // Threshold controls (only visible in Tolerance mode)
    m_thresholdContainer = new QWidget(m_toolBar);
    auto *thresholdLayout = new QHBoxLayout(m_thresholdContainer);
    thresholdLayout->setContentsMargins(0, 0, 0, 0);
    thresholdLayout->setSpacing(4);

    auto *thresholdLabel = new QLabel(tr("Threshold: "), m_thresholdContainer);
    thresholdLayout->addWidget(thresholdLabel);

    m_thresholdSlider = new QSlider(Qt::Horizontal, m_thresholdContainer);
    m_thresholdSlider->setRange(0, 255);
    m_thresholdSlider->setValue(m_threshold);
    m_thresholdSlider->setFixedWidth(150);
    m_thresholdSlider->setToolTip(tr("Tolerance map threshold (0-255)"));
    thresholdLayout->addWidget(m_thresholdSlider);

    m_thresholdValueLabel = new QLabel(QString(" %1 ").arg(m_threshold), m_thresholdContainer);
    m_thresholdValueLabel->setMinimumWidth(30);
    thresholdLayout->addWidget(m_thresholdValueLabel);

    m_toolBar->addWidget(m_thresholdContainer);
    m_thresholdContainer->setVisible(false); // Hidden by default (Swap mode)

    connect(m_thresholdSlider, &QSlider::valueChanged,
            this, &ComparePanel::onThresholdChanged);

    mainLayout->addWidget(m_toolBar);

    // ---- Scroll area containing the grid ----
    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);

    m_gridContainer = new QWidget(scrollArea);
    m_gridLayout = new QGridLayout(m_gridContainer);
    m_gridLayout->setContentsMargins(8, 8, 8, 8);
    m_gridLayout->setSpacing(8);

    scrollArea->setWidget(m_gridContainer);
    mainLayout->addWidget(scrollArea);
}

// ---- Mode toggle ----

void ComparePanel::onModeToggled()
{
    if (m_compareMode == SwapMode) {
        m_compareMode = ToleranceMode;
        m_modeAction->setText(tr("Mode: Tolerance"));
        m_thresholdContainer->setVisible(true);
    } else {
        m_compareMode = SwapMode;
        m_modeAction->setText(tr("Mode: Swap"));
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

// ---- Cell management ----

ComparePanel::ImageCell ComparePanel::createCell(const QString &folderPath)
{
    ImageCell cell;
    cell.folderPath = folderPath;
    cell.hasImage = false;

    cell.container = new QWidget(m_gridContainer);
    auto *cellLayout = new QVBoxLayout(cell.container);
    cellLayout->setContentsMargins(4, 4, 4, 4);
    cellLayout->setSpacing(2);

    QString folderName = QDir(folderPath).dirName();
    cell.headerLabel = new QLabel(
        QStringLiteral("<b>%1</b><br><i>No image selected</i>").arg(folderName),
        cell.container);
    cell.headerLabel->setAlignment(Qt::AlignCenter);
    cell.headerLabel->setStyleSheet(
        "QLabel { background-color: #e8e8e8; padding: 4px; border-radius: 3px; }");
    cellLayout->addWidget(cell.headerLabel);

    cell.imageContainer = new QWidget(cell.container);
    cell.imageContainer->setMinimumSize(200, 200);
    cell.imageContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    cell.imageLabel = new QLabel(cell.imageContainer);
    cell.imageLabel->setAlignment(Qt::AlignCenter);
    cell.imageLabel->setScaledContents(false);
    cell.imageLabel->setStyleSheet(
        "QLabel { background-color: #f0f0f0; border: 1px solid #ccc; }");
    cell.imageLabel->setText(tr("Click a thumbnail\nto compare"));

    cell.arrowOverlay = new ArrowOverlay(cell.imageContainer);

    cellLayout->addWidget(cell.imageContainer, 1);
    cell.imageContainer->installEventFilter(this);

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
    cell.cachedTolerancePixmap = QPixmap();
    cell.cachedOriginalPixmap = QPixmap();
    cell.cachedDisplaySize = QSize();

    if (cell.hasImage) {
        // Pre-compute the display pixmap at current size
        QSize displaySize = cell.imageLabel->size().expandedTo(QSize(200, 200));
        QPixmap pixmap = QPixmap::fromImage(cell.originalImage);
        cell.cachedOriginalPixmap = pixmap.scaled(
            displaySize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        cell.cachedDisplaySize = displaySize;
    }

    // Update geometry and display
    QRect r = cell.imageContainer->rect();
    cell.imageLabel->setGeometry(r);
    cell.arrowOverlay->setGeometry(r);
    cell.arrowOverlay->raise();
    showOriginalImage(cellIndex);
}

void ComparePanel::clearImage(int cellIndex)
{
    if (cellIndex < 0 || cellIndex >= m_cells.size()) return;

    ImageCell &cell = m_cells[cellIndex];
    cell.imagePath.clear();
    cell.originalImage = QImage();
    cell.cachedOriginalPixmap = QPixmap();
    cell.cachedToleranceImage = QImage();
    cell.cachedTolerancePixmap = QPixmap();
    cell.cachedDisplaySize = QSize();
    cell.hasImage = false;
    cell.showingToleranceMap = false;
    cell.toleranceSourceIndex = -1;
    cell.imageLabel->clear();
    cell.imageLabel->setText(tr("Click a thumbnail\nto compare"));

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
    cell.imageLabel->setGeometry(r);
    cell.arrowOverlay->setGeometry(r);
    cell.arrowOverlay->raise();

    if (!cell.hasImage) return;

    QSize displaySize = cell.imageLabel->size().expandedTo(QSize(200, 200));

    // Only re-scale if size actually changed
    if (displaySize == cell.cachedDisplaySize) return;

    cell.cachedDisplaySize = displaySize;

    // Re-scale original pixmap
    QPixmap origPixmap = QPixmap::fromImage(cell.originalImage);
    cell.cachedOriginalPixmap = origPixmap.scaled(
        displaySize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // Re-scale tolerance pixmap if we have a cached tolerance image
    if (!cell.cachedToleranceImage.isNull()) {
        QPixmap tolPixmap = QPixmap::fromImage(cell.cachedToleranceImage);
        cell.cachedTolerancePixmap = tolPixmap.scaled(
            displaySize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    // Update display
    if (cell.showingToleranceMap && !cell.cachedTolerancePixmap.isNull()) {
        cell.imageLabel->setPixmap(cell.cachedTolerancePixmap);
    } else {
        cell.imageLabel->setPixmap(cell.cachedOriginalPixmap);
    }
}

void ComparePanel::showOriginalImage(int cellIndex)
{
    if (cellIndex < 0 || cellIndex >= m_cells.size()) return;

    ImageCell &cell = m_cells[cellIndex];
    if (cell.originalImage.isNull()) {
        cell.imageLabel->setText(tr("Failed to load image"));
        return;
    }

    // Use cached pixmap if available
    if (cell.cachedOriginalPixmap.isNull()) {
        QSize displaySize = cell.imageLabel->size().expandedTo(QSize(200, 200));
        QPixmap pixmap = QPixmap::fromImage(cell.originalImage);
        cell.cachedOriginalPixmap = pixmap.scaled(
            displaySize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        cell.cachedDisplaySize = displaySize;
    }

    cell.imageLabel->setPixmap(cell.cachedOriginalPixmap);
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
        // Invalidate scaled cache
        target.cachedTolerancePixmap = QPixmap();
    }

    if (target.cachedToleranceImage.isNull()) return;

    // Scale for display if needed
    if (target.cachedTolerancePixmap.isNull()) {
        QSize displaySize = target.imageLabel->size().expandedTo(QSize(200, 200));
        QPixmap pixmap = QPixmap::fromImage(target.cachedToleranceImage);
        target.cachedTolerancePixmap = pixmap.scaled(
            displaySize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    target.imageLabel->setPixmap(target.cachedTolerancePixmap);
    target.showingToleranceMap = true;
    target.toleranceSourceIndex = sourceIndex;
}

void ComparePanel::showSourceOnTarget(int sourceIndex, int targetIndex)
{
    if (sourceIndex < 0 || sourceIndex >= m_cells.size()) return;
    if (targetIndex < 0 || targetIndex >= m_cells.size()) return;

    const ImageCell &source = m_cells[sourceIndex];
    ImageCell &target = m_cells[targetIndex];

    if (source.originalImage.isNull()) return;

    // Use source's cached pixmap if available and same size
    QSize displaySize = target.imageLabel->size().expandedTo(QSize(200, 200));
    if (!source.cachedOriginalPixmap.isNull()) {
        target.imageLabel->setPixmap(source.cachedOriginalPixmap);
    } else {
        QPixmap pixmap = QPixmap::fromImage(source.originalImage);
        target.imageLabel->setPixmap(pixmap.scaled(
            displaySize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
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
    m_thresholdValueLabel->setText(QString(" %1 ").arg(value));

    if (m_settingsManager) {
        m_settingsManager->setComparisonThreshold(value);
    }

    // Invalidate cached tolerance maps and regenerate
    for (int i = 0; i < m_cells.size(); ++i) {
        if (m_cells[i].showingToleranceMap && m_cells[i].toleranceSourceIndex >= 0) {
            m_cells[i].cachedToleranceImage = QImage(); // Force regeneration
            m_cells[i].cachedTolerancePixmap = QPixmap();
            showToleranceMap(m_cells[i].toleranceSourceIndex, i);
        }
    }
}
