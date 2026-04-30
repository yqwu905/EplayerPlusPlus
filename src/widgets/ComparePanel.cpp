#include "ComparePanel.h"
#include "ZoomableImageWidget.h"
#include "services/ImageComparer.h"
#include "services/ImageLoader.h"
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
#include <QPushButton>
#include <QCheckBox>
#include <QEvent>
#include <QKeyEvent>
#include <QApplication>
#include <QSet>
#include <QSizePolicy>

ComparePanel::ComparePanel(CompareSession *session,
                           SettingsManager *settingsManager,
                           ImageLoader *imageLoader,
                           QWidget *parent)
    : QWidget(parent)
    , m_session(session)
    , m_imageLoader(imageLoader)
    , m_settingsManager(settingsManager)
{
    if (m_settingsManager) {
        m_threshold = m_settingsManager->comparisonThreshold();
        m_resizeToFirstImageEnabled = m_settingsManager->resizeToFirstImageEnabled();
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

    if (m_imageLoader) {
        connect(m_imageLoader, &ImageLoader::imageReady,
                this, &ComparePanel::onImageReady);
        connect(m_imageLoader, &ImageLoader::thumbnailReady,
                this, &ComparePanel::onThumbnailReady);
    }
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

    m_resizeToFirstImageCheckBox = new QCheckBox(tr("Resize others to first"), m_toolBar);
    m_resizeToFirstImageCheckBox->setChecked(m_resizeToFirstImageEnabled);
    m_resizeToFirstImageCheckBox->setToolTip(
        tr("When enabled, non-primary images are resized to match the first image size"));
    connect(m_resizeToFirstImageCheckBox, &QCheckBox::toggled,
            this, &ComparePanel::onResizeToFirstImageToggled);
    m_toolBar->addWidget(m_resizeToFirstImageCheckBox);

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
    QHash<QString, QStringList> selectionMap;
    for (const auto &pair : selectedImages) {
        selectionMap[pair.first].append(pair.second);
    }

    preloadImagesForSelection(selectedImages);

    for (int i = 0; i < m_cells.size(); ++i) {
        auto it = selectionMap.find(m_cells[i].folderPath);
        if (it == selectionMap.end() || it->isEmpty()) {
            continue;
        }

        const QString newImagePath = it->takeFirst();
        if (newImagePath != m_cells[i].imagePath) {
            m_cells[i].imagePath = newImagePath;
            m_cells[i].showingToleranceMap = false;
            m_cells[i].toleranceSourceIndex = -1;
            loadImage(i);

            QString folderName = QDir(m_cells[i].folderPath).dirName();
            QString fileName = QFileInfo(newImagePath).fileName();
            m_cells[i].headerLabel->setText(
                QStringLiteral("%1 / %2").arg(folderName, fileName));
            m_cells[i].headerLabel->setToolTip(
                QStringLiteral("%1\n%2").arg(folderName, fileName));
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

    // ---- Header — title and compare buttons in one row ----
    auto *headerWidget = new QWidget(cell.container);
    headerWidget->setStyleSheet(
        "QWidget { background-color: #FFFFFF; border: none; "
        "border-top-left-radius: 8px; border-top-right-radius: 8px; "
        "border-bottom: 1px solid #E0E0E0; }");
    auto *headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(12, 6, 8, 6);
    headerLayout->setSpacing(8);

    QString folderName = QDir(folderPath).dirName();
    cell.headerLabel = new QLabel(
        QStringLiteral("%1 / %2").arg(folderName, tr("No image selected")),
        headerWidget);
    cell.headerLabel->setObjectName(QStringLiteral("compareCellHeaderLabel"));
    cell.headerLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    cell.headerLabel->setMinimumWidth(0);
    cell.headerLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    cell.headerLabel->setToolTip(folderName);
    cell.headerLabel->setStyleSheet(
        "QLabel { background-color: #FFFFFF; color: #1A1A1A; "
        "padding: 0px; border: none; font-size: 12px; font-weight: 600; }");
    headerLayout->addWidget(cell.headerLabel, 1);

    cell.compareButtonsContainer = new QWidget(headerWidget);
    cell.compareButtonsContainer->setObjectName(QStringLiteral("compareCellButtons"));
    cell.compareButtonsLayout = new QHBoxLayout(cell.compareButtonsContainer);
    cell.compareButtonsLayout->setContentsMargins(0, 0, 0, 0);
    cell.compareButtonsLayout->setSpacing(4);
    cell.compareButtonsContainer->setStyleSheet(
        "QWidget { background-color: #FFFFFF; border: none; }");
    headerLayout->addWidget(cell.compareButtonsContainer, 0, Qt::AlignRight | Qt::AlignVCenter);
    cellLayout->addWidget(headerWidget);

    // ---- Image container ----
    cell.imageContainer = new QWidget(cell.container);
    cell.imageContainer->setMinimumSize(200, 200);
    cell.imageContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    cell.imageContainer->setStyleSheet("QWidget { background-color: #F5F5F5; }");

    cell.imageWidget = new ZoomableImageWidget(cell.imageContainer);
    cell.imageWidget->setText(tr("Click a thumbnail\nto compare"));

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

    int cols = (count <= 3) ? count : ((count + 1) / 2);
    int rows = (count + cols - 1) / cols;

    // Reset historical stretch factors, otherwise a previous 2x3 layout can
    // keep occupying space after switching to 1x2 / 2x2.
    for (int r = 0; r < CompareSession::MaxFolders; ++r) {
        m_gridLayout->setRowStretch(r, 0);
    }
    for (int c = 0; c < CompareSession::MaxFolders; ++c) {
        m_gridLayout->setColumnStretch(c, 0);
    }

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

    rebuildCompareButtons();
}

void ComparePanel::rebuildCompareButtons()
{
    for (int i = 0; i < m_cells.size(); ++i) {
        setupCompareButtonsForCell(i);
    }
}

void ComparePanel::setupCompareButtonsForCell(int cellIndex)
{
    if (cellIndex < 0 || cellIndex >= m_cells.size()) return;

    ImageCell &cell = m_cells[cellIndex];
    qDeleteAll(cell.compareButtons);
    cell.compareButtons.clear();

    while (cell.compareButtonsLayout->count() > 0) {
        QLayoutItem *item = cell.compareButtonsLayout->takeAt(0);
        delete item;
    }

    const int count = m_cells.size();
    for (int targetIndex = 0; targetIndex < count; ++targetIndex) {
        if (targetIndex == cellIndex) {
            continue;
        }

        auto *button = new QPushButton(
            QString::number(targetIndex + 1), cell.compareButtonsContainer);
        const QString folderName = QDir(m_cells[targetIndex].folderPath).dirName();
        button->setToolTip(tr("使用当前图片与“%1”列对比").arg(folderName));
        button->setFixedSize(28, 26);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(
            "QPushButton {"
            "  border: 1px solid #0078D4;"
            "  border-radius: 4px;"
            "  background-color: #FFFFFF;"
            "  color: #0078D4;"
            "  font-weight: 600;"
            "  padding: 0px;"
            "}"
            "QPushButton:hover {"
            "  background-color: #E5F1FB;"
            "}"
            "QPushButton:pressed {"
            "  background-color: #0078D4;"
            "  color: #FFFFFF;"
            "}");

        connect(button, &QPushButton::pressed, this, [this, cellIndex, targetIndex]() {
            onComparePressed(cellIndex, targetIndex);
        });
        connect(button, &QPushButton::released, this, [this, cellIndex, targetIndex]() {
            onCompareReleased(cellIndex, targetIndex);
        });
        connect(button, &QPushButton::clicked, this, [this, cellIndex, targetIndex]() {
            onCompareClicked(cellIndex, targetIndex);
        });

        cell.compareButtonsLayout->addWidget(button);
        cell.compareButtons.append(button);
    }

    cell.compareButtonsLayout->addStretch();
}

void ComparePanel::loadImage(int cellIndex)
{
    if (cellIndex < 0 || cellIndex >= m_cells.size()) return;

    ImageCell &cell = m_cells[cellIndex];
    cell.previewImage = QImage();
    cell.showingPreview = false;
    if (m_imageLoader) {
        cell.originalImage = m_imageLoader->getCachedImage(cell.imagePath);
        if (cell.originalImage.isNull()) {
            const QImage cachedPreview = m_imageLoader->getCachedThumbnail(cell.imagePath);
            if (!cachedPreview.isNull()) {
                showPreviewImage(cellIndex, cachedPreview, true);
            } else {
                cell.imageWidget->setText(tr("Loading image..."));
            }
            m_imageLoader->requestThumbnail(cell.imagePath, QSize(960, 960));
            m_imageLoader->requestImage(cell.imagePath);
            cell.hasImage = false;
            cell.cachedToleranceImage = QImage();
            return;
        }
    } else {
        cell.originalImage = QImage(cell.imagePath);
    }
    cell.hasImage = !cell.originalImage.isNull();
    cell.showingPreview = false;
    cell.cachedToleranceImage = QImage();     // Invalidate tolerance cache

    // Update geometry
    QRect r = cell.imageContainer->rect();
    cell.imageWidget->setGeometry(r);

    showOriginalImage(cellIndex, true);  // resetView on initial load
}

void ComparePanel::preloadImagesForSelection(const QList<QPair<QString, QString>> &selectedImages)
{
    if (!m_imageLoader) {
        return;
    }

    QStringList uniquePaths;
    QSet<QString> seen;
    uniquePaths.reserve(selectedImages.size());
    for (const auto &pair : selectedImages) {
        if (!pair.second.isEmpty() && !seen.contains(pair.second)) {
            seen.insert(pair.second);
            uniquePaths.append(pair.second);
        }
    }

    if (!uniquePaths.isEmpty()) {
        m_imageLoader->cancelImageRequestsExcept(seen);
        m_imageLoader->requestImageBatch(uniquePaths);
    }
}

void ComparePanel::clearImage(int cellIndex)
{
    if (cellIndex < 0 || cellIndex >= m_cells.size()) return;

    ImageCell &cell = m_cells[cellIndex];
    cell.imagePath.clear();
    cell.originalImage = QImage();
    cell.previewImage = QImage();
    cell.cachedToleranceImage = QImage();
    cell.hasImage = false;
    cell.showingPreview = false;
    cell.showingToleranceMap = false;
    cell.toleranceSourceIndex = -1;
    cell.imageWidget->setText(tr("Click a thumbnail\nto compare"));

    QString folderName = QDir(cell.folderPath).dirName();
    cell.headerLabel->setText(
        QStringLiteral("%1 / %2").arg(folderName, tr("No image selected")));
    cell.headerLabel->setToolTip(folderName);
}

void ComparePanel::resizeImageCell(int cellIndex)
{
    if (cellIndex < 0 || cellIndex >= m_cells.size()) return;

    ImageCell &cell = m_cells[cellIndex];
    if (!cell.imageContainer) return;

    QRect r = cell.imageContainer->rect();
    cell.imageWidget->setGeometry(r);

    // ZoomableImageWidget handles redraw internally on resize
}

void ComparePanel::showPreviewImage(int cellIndex, const QImage &preview, bool resetView)
{
    if (cellIndex < 0 || cellIndex >= m_cells.size() || preview.isNull()) return;

    ImageCell &cell = m_cells[cellIndex];
    cell.previewImage = preview;
    cell.showingPreview = true;
    cell.imageWidget->setImage(preview, resetView);
}

void ComparePanel::showOriginalImage(int cellIndex, bool resetView)
{
    if (cellIndex < 0 || cellIndex >= m_cells.size()) return;

    ImageCell &cell = m_cells[cellIndex];
    const QImage displayImage = imageForCompare(cellIndex);
    if (displayImage.isNull()) {
        cell.imageWidget->setText(tr("Failed to load image"));
        return;
    }

    cell.imageWidget->setImage(displayImage, resetView);
    cell.showingPreview = false;
    cell.showingToleranceMap = false;
    cell.toleranceSourceIndex = -1;
}

void ComparePanel::showToleranceMap(int sourceIndex, int targetIndex)
{
    if (sourceIndex < 0 || sourceIndex >= m_cells.size()) return;
    if (targetIndex < 0 || targetIndex >= m_cells.size()) return;

    ImageCell &target = m_cells[targetIndex];
    const QImage sourceImage = imageForCompare(sourceIndex);
    const QImage targetImage = imageForCompare(targetIndex);
    if (sourceImage.isNull() || targetImage.isNull()) return;

    // Only regenerate the tolerance image if source or threshold changed
    bool needRegenerate = target.cachedToleranceImage.isNull()
                          || target.toleranceSourceIndex != sourceIndex;

    if (needRegenerate) {
        target.cachedToleranceImage = ImageComparer::generateToleranceMap(
            sourceImage, targetImage, m_threshold);
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

    const QImage sourceImage = imageForCompare(sourceIndex);
    if (sourceImage.isNull()) return;
    m_cells[targetIndex].imageWidget->setImage(sourceImage, false);
}

// ---- Compare interaction (mode-dependent) ----

void ComparePanel::onComparePressed(int sourceIndex, int targetIndex)
{
    if (m_compareMode == SwapMode) {
        showSourceOnTarget(sourceIndex, targetIndex);
    }
    // In ToleranceMode, press does nothing
}

void ComparePanel::onCompareReleased(int sourceIndex, int targetIndex)
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

void ComparePanel::onCompareClicked(int sourceIndex, int targetIndex)
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

void ComparePanel::onResizeToFirstImageToggled(bool enabled)
{
    m_resizeToFirstImageEnabled = enabled;

    if (m_settingsManager) {
        m_settingsManager->setResizeToFirstImageEnabled(enabled);
    }

    for (int i = 0; i < m_cells.size(); ++i) {
        m_cells[i].cachedToleranceImage = QImage();
        if (m_cells[i].showingToleranceMap && m_cells[i].toleranceSourceIndex >= 0) {
            showToleranceMap(m_cells[i].toleranceSourceIndex, i);
        } else {
            showOriginalImage(i);
        }
    }
}

QImage ComparePanel::imageForCompare(int cellIndex) const
{
    if (cellIndex < 0 || cellIndex >= m_cells.size()) return QImage();

    const QImage &original = m_cells[cellIndex].originalImage;
    if (original.isNull()) return QImage();

    if (!m_resizeToFirstImageEnabled || cellIndex == 0 || m_cells.isEmpty()) {
        return original;
    }

    const QImage &firstImage = m_cells.first().originalImage;
    if (firstImage.isNull()) {
        return original;
    }

    if (original.size() == firstImage.size()) {
        return original;
    }

    return original.scaled(firstImage.size(),
                           Qt::IgnoreAspectRatio,
                           Qt::SmoothTransformation);
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

void ComparePanel::onImageReady(const QString &imagePath, const QImage &image)
{
    if (image.isNull()) {
        return;
    }

    for (int i = 0; i < m_cells.size(); ++i) {
        if (m_cells[i].imagePath != imagePath) {
            continue;
        }

        m_cells[i].originalImage = image;
        m_cells[i].hasImage = true;
        m_cells[i].showingPreview = false;
        m_cells[i].cachedToleranceImage = QImage();
        showOriginalImage(i, true);
    }
}

void ComparePanel::onThumbnailReady(const QString &imagePath, const QImage &thumbnail)
{
    if (thumbnail.isNull()) {
        return;
    }

    for (int i = 0; i < m_cells.size(); ++i) {
        ImageCell &cell = m_cells[i];
        if (cell.imagePath != imagePath || cell.hasImage) {
            continue;
        }

        const QImage currentPreview = cell.previewImage;
        if (!currentPreview.isNull() &&
            currentPreview.width() * currentPreview.height() >=
                thumbnail.width() * thumbnail.height()) {
            continue;
        }

        showPreviewImage(i, thumbnail, currentPreview.isNull());
    }
}
