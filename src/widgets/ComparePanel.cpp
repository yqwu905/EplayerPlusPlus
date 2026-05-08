#include "ComparePanel.h"
#include "ImageContextMenu.h"
#include "ZoomableImageWidget.h"
#include "services/ImageComparer.h"
#include "services/ImageLoader.h"
#include "services/ImageMarkManager.h"
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
#include <QDataStream>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QIODevice>
#include <QKeyEvent>
#include <QKeySequence>
#include <QApplication>
#include <QMimeData>
#include <QSet>
#include <QSizePolicy>
#include <QInputDialog>
#include <QLineEdit>
#include <QSignalBlocker>

namespace
{
const QColor kCompareColors[] = {
    QColor(0x18, 0x6F, 0xD7),
    QColor(0x22, 0x8A, 0x46),
    QColor(0xF7, 0x73, 0x13),
    QColor(0x74, 0x55, 0xC8),
    QColor(0x0F, 0x7B, 0x93),
    QColor(0xC5, 0x0F, 0x1F)
};
const int kCompareColorCount = sizeof(kCompareColors) / sizeof(kCompareColors[0]);
const char kFolderOrderMimeType[] = "application/x-eplayer-folder-index";

QColor compareColor(int index)
{
    return kCompareColors[qMax(0, index) % kCompareColorCount];
}

QString colorStyle(const QColor &color)
{
    return color.name(QColor::HexRgb);
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
}

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
    connect(m_session, &CompareSession::foldersSwapped,
            this, &ComparePanel::onFoldersSwapped);
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

void ComparePanel::setImageMarkManager(ImageMarkManager *manager)
{
    if (m_markManager) {
        disconnect(m_markManager, nullptr, this, nullptr);
    }

    m_markManager = manager;

    if (m_markManager) {
        connect(m_markManager, &ImageMarkManager::markChanged,
                this, &ComparePanel::onMarkChanged);
        for (const QString &folderPath : m_session->folders()) {
            m_markManager->loadFolder(folderPath);
        }
    }

    for (int i = 0; i < m_cells.size(); ++i) {
        setupMarkButtonsForCell(i);
        updateMarkButtonsForCell(i);
    }
}

void ComparePanel::setControlsVisible(bool visible)
{
    if (m_toolBar) {
        m_toolBar->setVisible(visible);
    }
}

void ComparePanel::setCompareMode(CompareMode mode)
{
    if (m_compareMode == mode) {
        if (m_modeAction) {
            m_modeAction->setChecked(mode == ToleranceMode);
        }
        if (m_thresholdContainer) {
            m_thresholdContainer->setVisible(mode == ToleranceMode && m_toolBar && m_toolBar->isVisible());
        }
        return;
    }

    m_compareMode = mode;

    if (m_modeAction) {
        m_modeAction->setText(mode == ToleranceMode ? tr("容差图") : tr("交换"));
        m_modeAction->setChecked(mode == ToleranceMode);
    }
    if (m_thresholdContainer) {
        m_thresholdContainer->setVisible(mode == ToleranceMode && m_toolBar && m_toolBar->isVisible());
    }

    if (mode == SwapMode) {
        for (int i = 0; i < m_cells.size(); ++i) {
            if (m_cells[i].showingToleranceMap) {
                showOriginalImage(i);
            }
        }
    }

    emit compareModeChanged(mode);
}

void ComparePanel::setComparisonThreshold(int value)
{
    const int clamped = qBound(0, value, 255);
    if (m_threshold == clamped) {
        if (m_thresholdSlider && m_thresholdSlider->value() != clamped) {
            const QSignalBlocker blocker(m_thresholdSlider);
            m_thresholdSlider->setValue(clamped);
        }
        if (m_thresholdValueLabel) {
            m_thresholdValueLabel->setText(QString::number(clamped));
        }
        return;
    }

    m_threshold = clamped;

    if (m_thresholdSlider && m_thresholdSlider->value() != clamped) {
        const QSignalBlocker blocker(m_thresholdSlider);
        m_thresholdSlider->setValue(clamped);
    }
    if (m_thresholdValueLabel) {
        m_thresholdValueLabel->setText(QString::number(clamped));
    }

    if (m_settingsManager) {
        m_settingsManager->setComparisonThreshold(clamped);
    }

    for (int i = 0; i < m_cells.size(); ++i) {
        if (m_cells[i].showingToleranceMap && m_cells[i].toleranceSourceIndex >= 0) {
            m_cells[i].cachedToleranceImage = QImage();
            showToleranceMap(m_cells[i].toleranceSourceIndex, i);
        }
    }

    emit comparisonThresholdChanged(clamped);
}

void ComparePanel::setResizeToFirstImageEnabled(bool enabled)
{
    if (m_resizeToFirstImageEnabled == enabled) {
        if (m_resizeToFirstImageCheckBox && m_resizeToFirstImageCheckBox->isChecked() != enabled) {
            const QSignalBlocker blocker(m_resizeToFirstImageCheckBox);
            m_resizeToFirstImageCheckBox->setChecked(enabled);
        }
        return;
    }

    m_resizeToFirstImageEnabled = enabled;

    if (m_resizeToFirstImageCheckBox && m_resizeToFirstImageCheckBox->isChecked() != enabled) {
        const QSignalBlocker blocker(m_resizeToFirstImageCheckBox);
        m_resizeToFirstImageCheckBox->setChecked(enabled);
    }

    if (m_settingsManager) {
        m_settingsManager->setResizeToFirstImageEnabled(enabled);
    }

    for (int i = 0; i < m_cells.size(); ++i) {
        m_cells[i].cachedToleranceImage = QImage();
        if (m_cells[i].showingToleranceMap && m_cells[i].toleranceSourceIndex >= 0) {
            showToleranceMap(m_cells[i].toleranceSourceIndex, i);
        } else if (m_cells[i].hasImage) {
            showOriginalImage(i);
        }
    }

    emit resizeToFirstImageChanged(enabled);
}

void ComparePanel::setupUi()
{
    setObjectName(QStringLiteral("comparePanelRoot"));
    setStyleSheet(
        "QWidget#comparePanelRoot { background-color: #FFFFFF; border: 1px solid #E3E7EC; border-radius: 8px; }");
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
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

    m_resizeToFirstImageCheckBox = new QCheckBox(tr("同步尺寸"), m_toolBar);
    m_resizeToFirstImageCheckBox->setChecked(m_resizeToFirstImageEnabled);
    m_resizeToFirstImageCheckBox->setToolTip(
        tr("When enabled, non-primary images are resized to match the first image size"));
    connect(m_resizeToFirstImageCheckBox, &QCheckBox::toggled,
            this, &ComparePanel::onResizeToFirstImageToggled);
    m_toolBar->addWidget(m_resizeToFirstImageCheckBox);

    m_toolBar->addSeparator();

    // Mode toggle button — Fluent 2 pill / toggle style
    m_modeAction = m_toolBar->addAction(tr("交换"));
    m_modeAction->setToolTip(tr("Click to switch between Swap and Tolerance mode"));
    m_modeAction->setCheckable(true);
    connect(m_modeAction, &QAction::triggered, this, &ComparePanel::onModeToggled);

    m_toolBar->addSeparator();

    // Threshold controls (only visible in Tolerance mode)
    m_thresholdContainer = new QWidget(m_toolBar);
    auto *thresholdLayout = new QHBoxLayout(m_thresholdContainer);
    thresholdLayout->setContentsMargins(0, 0, 0, 0);
    thresholdLayout->setSpacing(8);

    auto *thresholdLabel = new QLabel(tr("阈值"), m_thresholdContainer);
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
        "QScrollArea { background-color: #FFFFFF; border: none; }");

    m_gridContainer = new QWidget(scrollArea);
    m_gridContainer->setStyleSheet("QWidget { background-color: #FFFFFF; }");
    m_gridLayout = new QGridLayout(m_gridContainer);
    m_gridLayout->setContentsMargins(0, 0, 0, 0);
    m_gridLayout->setSpacing(8);

    scrollArea->setWidget(m_gridContainer);
    mainLayout->addWidget(scrollArea);

    for (const QString &category : ImageMarkManager::categories()) {
        auto *markAction = new QAction(this);
        markAction->setShortcut(QKeySequence(category));
        markAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        markAction->setToolTip(tr("Mark compared images as %1").arg(category));
        addAction(markAction);
        connect(markAction, &QAction::triggered, this, [this, category]() {
            markAllCurrentImages(category);
        });
    }
}

// ---- Mode toggle ----

void ComparePanel::onModeToggled()
{
    setCompareMode(m_compareMode == SwapMode ? ToleranceMode : SwapMode);
}

// ---- Session change handlers ----

void ComparePanel::onFolderAdded(const QString &folderPath, int /*index*/)
{
    if (m_markManager) {
        m_markManager->loadFolder(folderPath);
    }
    ImageCell cell = createCell(folderPath);
    m_cells.append(cell);
    setupMarkButtonsForCell(m_cells.size() - 1);
    rebuildGrid();
}

void ComparePanel::onFolderRemoved(const QString &folderPath, int index)
{
    int removeIndex = index;
    if (removeIndex < 0 || removeIndex >= m_cells.size() ||
        m_cells[removeIndex].folderPath != folderPath) {
        removeIndex = -1;
        for (int i = 0; i < m_cells.size(); ++i) {
            if (m_cells[i].folderPath == folderPath) {
                removeIndex = i;
                break;
            }
        }
    }

    if (removeIndex < 0 || removeIndex >= m_cells.size()) {
        return;
    }

    if (m_cells[removeIndex].container) {
        m_cells[removeIndex].container->removeEventFilter(this);
    }
    if (m_cells[removeIndex].headerWidget) {
        m_cells[removeIndex].headerWidget->removeEventFilter(this);
    }
    if (m_cells[removeIndex].indexBadge) {
        m_cells[removeIndex].indexBadge->removeEventFilter(this);
    }
    if (m_cells[removeIndex].headerLabel) {
        m_cells[removeIndex].headerLabel->removeEventFilter(this);
    }
    if (m_cells[removeIndex].imageContainer) {
        m_cells[removeIndex].imageContainer->removeEventFilter(this);
    }
    if (m_cells[removeIndex].imageWidget) {
        m_cells[removeIndex].imageWidget->removeEventFilter(this);
    }

    m_gridLayout->removeWidget(m_cells[removeIndex].container);
    delete m_cells[removeIndex].container;
    m_cells.removeAt(removeIndex);
    rebuildGrid();
}

void ComparePanel::onFoldersSwapped(int firstIndex, int secondIndex)
{
    if (firstIndex < 0 || firstIndex >= m_cells.size() ||
        secondIndex < 0 || secondIndex >= m_cells.size() ||
        firstIndex == secondIndex) {
        return;
    }

    auto remapIndex = [firstIndex, secondIndex](int index) {
        if (index == firstIndex) {
            return secondIndex;
        }
        if (index == secondIndex) {
            return firstIndex;
        }
        return index;
    };

    m_cells.swapItemsAt(firstIndex, secondIndex);

    for (auto &cell : m_cells) {
        if (cell.toleranceSourceIndex >= 0) {
            cell.toleranceSourceIndex = remapIndex(cell.toleranceSourceIndex);
            cell.cachedToleranceImage = QImage();
        }
    }

    rebuildGrid();

    for (int i = 0; i < m_cells.size(); ++i) {
        updateCellHeader(i);
        updateMarkButtonsForCell(i);
        if (m_cells[i].showingToleranceMap && m_cells[i].toleranceSourceIndex >= 0) {
            showToleranceMap(m_cells[i].toleranceSourceIndex, i);
        } else if (m_resizeToFirstImageEnabled && m_cells[i].hasImage) {
            showOriginalImage(i);
        }
    }
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

    bool firstImagePathChanged = false;
    for (int i = 0; i < m_cells.size(); ++i) {
        auto it = selectionMap.find(m_cells[i].folderPath);
        if (it == selectionMap.end() || it->isEmpty()) {
            continue;
        }

        const QString newImagePath = it->takeFirst();
        if (newImagePath != m_cells[i].imagePath) {
            if (i == 0) {
                firstImagePathChanged = true;
            }
            m_cells[i].imagePath = newImagePath;
            m_cells[i].showingToleranceMap = false;
            m_cells[i].toleranceSourceIndex = -1;
            loadImage(i);

            updateCellHeader(i);
        }
        updateMarkButtonsForCell(i);
    }

    if (firstImagePathChanged) {
        refreshCellsUsingFirstImage();
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

    const int dragTargetIndex = findCellByDragObject(watched);
    if (dragTargetIndex >= 0) {
        if (event->type() == QEvent::DragEnter) {
            auto *dragEvent = static_cast<QDragEnterEvent *>(event);
            int sourceIndex = -1;
            if (decodeFolderOrderDragIndex(dragEvent->mimeData(), &sourceIndex) &&
                sourceIndex >= 0 && sourceIndex != dragTargetIndex) {
                dragEvent->acceptProposedAction();
                return true;
            }
        } else if (event->type() == QEvent::DragMove) {
            auto *dragEvent = static_cast<QDragMoveEvent *>(event);
            int sourceIndex = -1;
            if (decodeFolderOrderDragIndex(dragEvent->mimeData(), &sourceIndex) &&
                sourceIndex >= 0 && sourceIndex != dragTargetIndex) {
                dragEvent->acceptProposedAction();
                return true;
            }
        } else if (event->type() == QEvent::Drop) {
            auto *dropEvent = static_cast<QDropEvent *>(event);
            int sourceIndex = -1;
            if (decodeFolderOrderDragIndex(dropEvent->mimeData(), &sourceIndex) &&
                sourceIndex >= 0 && sourceIndex != dragTargetIndex && m_session) {
                m_session->swapFolders(sourceIndex, dragTargetIndex);
                dropEvent->acceptProposedAction();
                return true;
            }
        } else if (isCellDragHandle(watched)) {
            if (event->type() == QEvent::MouseButtonPress) {
                auto *mouseEvent = static_cast<QMouseEvent *>(event);
                if (mouseEvent->button() == Qt::LeftButton) {
                    m_cellDragStartPos = mouseEvent->pos();
                    m_cellDragSourceObject = watched;
                    m_cellDragSourceIndex = dragTargetIndex;
                }
            } else if (event->type() == QEvent::MouseMove) {
                auto *mouseEvent = static_cast<QMouseEvent *>(event);
                if ((mouseEvent->buttons() & Qt::LeftButton) &&
                    m_cellDragSourceObject == watched &&
                    m_cellDragSourceIndex == dragTargetIndex &&
                    (mouseEvent->pos() - m_cellDragStartPos).manhattanLength() >=
                        QApplication::startDragDistance()) {
                    startCellDrag(dragTargetIndex);
                    mouseEvent->accept();
                    return true;
                }
            } else if (event->type() == QEvent::MouseButtonRelease) {
                auto *mouseEvent = static_cast<QMouseEvent *>(event);
                if (mouseEvent->button() == Qt::LeftButton &&
                    m_cellDragSourceObject == watched) {
                    m_cellDragSourceObject = nullptr;
                    m_cellDragSourceIndex = -1;
                }
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

    if (event->modifiers() == Qt::NoModifier) {
        const QString category = QKeySequence(event->key()).toString().toUpper();
        if (ImageMarkManager::isValidCategory(category)) {
            markAllCurrentImages(category);
            event->accept();
            return;
        }
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
        "border: none; border-radius: 8px; }");
    cell.container->setObjectName("compareCellContainer");
    cell.container->setAcceptDrops(true);
    cell.container->installEventFilter(this);
    auto *cellLayout = new QVBoxLayout(cell.container);
    cellLayout->setContentsMargins(0, 0, 0, 0);
    cellLayout->setSpacing(0);

    // ---- Header — image identity, compare buttons, and mark buttons ----
    cell.headerWidget = new QWidget(cell.container);
    auto *headerWidget = cell.headerWidget;
    headerWidget->setFixedHeight(72);
    headerWidget->setStyleSheet(
        "QWidget { background-color: #FFFFFF; border: none; }");
    headerWidget->setAcceptDrops(true);
    headerWidget->setCursor(Qt::OpenHandCursor);
    headerWidget->installEventFilter(this);
    auto *headerLayout = new QVBoxLayout(headerWidget);
    headerLayout->setContentsMargins(0, 0, 0, 8);
    headerLayout->setSpacing(8);

    auto *identityRow = new QHBoxLayout();
    identityRow->setContentsMargins(0, 0, 0, 0);
    identityRow->setSpacing(8);

    cell.indexBadge = new QLabel(headerWidget);
    cell.indexBadge->setObjectName(QStringLiteral("compareCellIndexBadge"));
    cell.indexBadge->setAlignment(Qt::AlignCenter);
    cell.indexBadge->setFixedSize(22, 22);
    cell.indexBadge->setAcceptDrops(true);
    cell.indexBadge->setCursor(Qt::OpenHandCursor);
    cell.indexBadge->installEventFilter(this);
    identityRow->addWidget(cell.indexBadge, 0, Qt::AlignTop);

    QString folderName = QDir(folderPath).dirName();
    cell.headerLabel = new QLabel(
        QStringLiteral("%1\n%2").arg(tr("未选择图片"), folderName),
        headerWidget);
    cell.headerLabel->setObjectName(QStringLiteral("compareCellHeaderLabel"));
    cell.headerLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    cell.headerLabel->setMinimumWidth(0);
    cell.headerLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    cell.headerLabel->setToolTip(folderName);
    cell.headerLabel->setStyleSheet(
        "QLabel { background-color: #FFFFFF; color: #263241; "
        "padding: 0px; border: none; font-size: 11px; font-weight: 500; line-height: 16px; }");
    cell.headerLabel->setAcceptDrops(true);
    cell.headerLabel->setCursor(Qt::OpenHandCursor);
    cell.headerLabel->installEventFilter(this);
    identityRow->addWidget(cell.headerLabel, 1);

    cell.renameButton = new QPushButton(QStringLiteral("✎"), headerWidget);
    cell.renameButton->setObjectName(QStringLiteral("compareCellRenameButton"));
    cell.renameButton->setFixedSize(24, 24);
    cell.renameButton->setToolTip(tr("Rename this comparison grid"));
    cell.renameButton->setCursor(Qt::PointingHandCursor);
    cell.renameButton->setStyleSheet(
        "QPushButton {"
        "  border: 1px solid transparent;"
        "  border-radius: 5px;"
        "  background-color: transparent;"
        "  color: #616161;"
        "  font-weight: 600;"
        "  padding: 0px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #F5F5F5;"
        "  border-color: #D1D1D1;"
        "  color: #1A1A1A;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #E5F1FB;"
        "  border-color: #0078D4;"
        "  color: #0078D4;"
        "}");
    QWidget *cellContainer = cell.container;
    connect(cell.renameButton, &QPushButton::clicked, this, [this, cellContainer]() {
        for (int i = 0; i < m_cells.size(); ++i) {
            if (m_cells[i].container == cellContainer) {
                renameCell(i);
                return;
            }
        }
    });
    identityRow->addWidget(cell.renameButton, 0, Qt::AlignTop);
    headerLayout->addLayout(identityRow);

    auto *controlsRow = new QHBoxLayout();
    controlsRow->setContentsMargins(0, 0, 0, 0);
    controlsRow->setSpacing(8);

    cell.compareButtonsContainer = new QWidget(headerWidget);
    cell.compareButtonsContainer->setObjectName(QStringLiteral("compareCellButtons"));
    cell.compareButtonsLayout = new QHBoxLayout(cell.compareButtonsContainer);
    cell.compareButtonsLayout->setContentsMargins(0, 0, 0, 0);
    cell.compareButtonsLayout->setSpacing(3);
    cell.compareButtonsContainer->setStyleSheet(
        "QWidget { background-color: #FFFFFF; border: none; }");
    controlsRow->addWidget(cell.compareButtonsContainer, 0, Qt::AlignLeft | Qt::AlignVCenter);
    controlsRow->addStretch();

    cell.markButtonsContainer = new QWidget(headerWidget);
    cell.markButtonsContainer->setObjectName(QStringLiteral("imageMarkButtons"));
    cell.markButtonsLayout = new QHBoxLayout(cell.markButtonsContainer);
    cell.markButtonsLayout->setContentsMargins(0, 0, 0, 0);
    cell.markButtonsLayout->setSpacing(3);
    cell.markButtonsContainer->setStyleSheet(
        "QWidget { background-color: transparent; border: none; }");
    controlsRow->addWidget(cell.markButtonsContainer, 0, Qt::AlignRight | Qt::AlignVCenter);

    headerLayout->addLayout(controlsRow);
    cellLayout->addWidget(headerWidget);

    // ---- Image container ----
    cell.imageContainer = new QWidget(cell.container);
    cell.imageContainer->setMinimumSize(200, 200);
    cell.imageContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    cell.imageContainer->setContextMenuPolicy(Qt::CustomContextMenu);
    cell.imageContainer->setAcceptDrops(true);
    cell.imageContainer->setStyleSheet(
        "QWidget { background-color: #F8FAFC; border: 1px solid #EEF1F5; border-radius: 6px; }");

    cell.imageWidget = new ZoomableImageWidget(cell.imageContainer);
    cell.imageWidget->setText(tr("点击缩略图\n开始对比"));
    cell.imageWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    cell.imageWidget->setAcceptDrops(true);

    cellLayout->addWidget(cell.imageContainer, 1);
    cell.imageContainer->installEventFilter(this);
    cell.imageWidget->installEventFilter(this);

    QWidget *cellContainerForMenu = cell.container;
    connect(cell.imageContainer,
            &QWidget::customContextMenuRequested,
            this,
            [this, cellContainerForMenu, sourceWidget = cell.imageContainer](const QPoint &pos) {
        showImageContextMenuForCell(cellContainerForMenu, sourceWidget, pos);
    });
    connect(cell.imageWidget,
            &QWidget::customContextMenuRequested,
            this,
            [this, cellContainerForMenu, sourceWidget = cell.imageWidget](const QPoint &pos) {
        showImageContextMenuForCell(cellContainerForMenu, sourceWidget, pos);
    });

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
        if (cell.container) {
            cell.container->removeEventFilter(this);
        }
        if (cell.headerWidget) {
            cell.headerWidget->removeEventFilter(this);
        }
        if (cell.indexBadge) {
            cell.indexBadge->removeEventFilter(this);
        }
        if (cell.headerLabel) {
            cell.headerLabel->removeEventFilter(this);
        }
        if (cell.imageContainer) {
            cell.imageContainer->removeEventFilter(this);
        }
        if (cell.imageWidget) {
            cell.imageWidget->removeEventFilter(this);
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
        const QColor color = compareColor(i);
        if (m_cells[i].indexBadge) {
            m_cells[i].indexBadge->setText(QString::number(i + 1));
            m_cells[i].indexBadge->setStyleSheet(
                QStringLiteral("QLabel { background: %1; color: #FFFFFF; "
                               "border: none; border-radius: 5px; font-size: 12px; "
                               "font-weight: 700; }")
                    .arg(colorStyle(color)));
        }
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
        button->setObjectName(QStringLiteral("compareTargetButton"));
        button->setToolTip(tr("使用当前图片与“%1”列对比").arg(cellDisplayName(targetIndex)));
        button->setFixedSize(24, 22);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(
            "QPushButton {"
            "  border: 1px solid #D5DCE6;"
            "  border-radius: 4px;"
            "  background-color: #FFFFFF;"
            "  color: #283447;"
            "  font-weight: 600;"
            "  font-size: 11px;"
            "  padding: 0px;"
            "}"
            "QPushButton:hover {"
            "  background-color: #EFF6FF;"
            "  border-color: #2D7FF9;"
            "  color: #186FD7;"
            "}"
            "QPushButton:pressed {"
            "  background-color: #2D7FF9;"
            "  border-color: #2D7FF9;"
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

void ComparePanel::setupMarkButtonsForCell(int cellIndex)
{
    if (cellIndex < 0 || cellIndex >= m_cells.size()) return;

    ImageCell &cell = m_cells[cellIndex];
    cell.markButtons.clear();

    while (cell.markButtonsLayout && cell.markButtonsLayout->count() > 0) {
        QLayoutItem *item = cell.markButtonsLayout->takeAt(0);
        delete item->widget();
        delete item;
    }

    if (!cell.markButtonsContainer || !cell.markButtonsLayout) {
        return;
    }

    cell.markButtonsContainer->setVisible(m_markManager != nullptr);
    if (!m_markManager) {
        return;
    }

    QWidget *cellContainer = cell.container;
    for (const QString &category : ImageMarkManager::categories()) {
        auto *button = new QPushButton(category, cell.markButtonsContainer);
        button->setObjectName(QStringLiteral("imageMarkButton_%1").arg(category));
        button->setToolTip(tr("Mark as %1. Ctrl+click marks all compared images.").arg(category));
        button->setFixedSize(24, 22);
        button->setCursor(Qt::PointingHandCursor);
        connect(button, &QPushButton::clicked, this, [this, cellContainer, category]() {
            if ((QApplication::keyboardModifiers() & Qt::ControlModifier) != 0) {
                markAllCurrentImages(category);
                return;
            }

            for (int i = 0; i < m_cells.size(); ++i) {
                if (m_cells[i].container == cellContainer) {
                    markCell(i, category);
                    return;
                }
            }
        });
        cell.markButtonsLayout->addWidget(button);
        cell.markButtons.append(button);
    }

    cell.markButtonsContainer->adjustSize();
    positionMarkButtonsForCell(cellIndex);
    updateMarkButtonsForCell(cellIndex);
}

void ComparePanel::positionMarkButtonsForCell(int cellIndex)
{
    if (cellIndex < 0 || cellIndex >= m_cells.size()) return;

    ImageCell &cell = m_cells[cellIndex];
    if (!cell.imageContainer || !cell.markButtonsContainer) {
        return;
    }

    if (cell.markButtonsContainer->parentWidget() != cell.imageContainer) {
        cell.markButtonsContainer->adjustSize();
        return;
    }

    cell.markButtonsContainer->adjustSize();
    const QSize buttonSize = cell.markButtonsContainer->sizeHint();
    const int x = qMax(8, cell.imageContainer->width() - buttonSize.width() - 10);
    const int y = 10;
    cell.markButtonsContainer->setGeometry(QRect(QPoint(x, y), buttonSize));
    cell.markButtonsContainer->raise();
}

void ComparePanel::updateMarkButtonsForCell(int cellIndex)
{
    if (cellIndex < 0 || cellIndex >= m_cells.size()) return;

    ImageCell &cell = m_cells[cellIndex];
    const QString currentMark = markForCell(cellIndex);
    for (QPushButton *button : cell.markButtons) {
        const bool active = (button->text() == currentMark);
        button->setStyleSheet(active
            ? QStringLiteral(
                  "QPushButton { border: 1px solid #2D7FF9; border-radius: 4px; "
                  "background-color: #EAF4FF; color: #186FD7; font-weight: 700; "
                  "font-size: 11px; padding: 0px; }"
                  "QPushButton:hover { background-color: #DDEEFF; }")
            : QStringLiteral(
                  "QPushButton { border: 1px solid #E0E5ED; border-radius: 4px; "
                  "background-color: #FFFFFF; color: #344054; font-weight: 600; "
                  "font-size: 11px; padding: 0px; }"
                  "QPushButton:hover { background-color: #F7FAFD; border-color: #B9C4D3; }"
                  "QPushButton:pressed { background-color: #EAF4FF; border-color: #2D7FF9; }"));
    }
}

void ComparePanel::updateAllMarkButtons()
{
    for (int i = 0; i < m_cells.size(); ++i) {
        updateMarkButtonsForCell(i);
    }
}

void ComparePanel::markCell(int cellIndex, const QString &category)
{
    if (!m_markManager || !ImageMarkManager::isValidCategory(category)) {
        return;
    }
    if (cellIndex < 0 || cellIndex >= m_cells.size()) {
        return;
    }

    const ImageCell &cell = m_cells[cellIndex];
    if (cell.folderPath.isEmpty() || cell.imagePath.isEmpty()) {
        return;
    }

    m_markManager->setMarkForImage(cell.folderPath, cell.imagePath, category);
}

void ComparePanel::markAllCurrentImages(const QString &category)
{
    if (!m_markManager || !ImageMarkManager::isValidCategory(category)) {
        return;
    }

    for (int i = 0; i < m_cells.size(); ++i) {
        markCell(i, category);
    }
}

QString ComparePanel::markForCell(int cellIndex) const
{
    if (!m_markManager || cellIndex < 0 || cellIndex >= m_cells.size()) {
        return QString();
    }

    const ImageCell &cell = m_cells[cellIndex];
    if (cell.folderPath.isEmpty() || cell.imagePath.isEmpty()) {
        return QString();
    }

    return m_markManager->markForImage(cell.folderPath, cell.imagePath);
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

    updateCellHeader(cellIndex);
    updateMarkButtonsForCell(cellIndex);
}

void ComparePanel::resizeImageCell(int cellIndex)
{
    if (cellIndex < 0 || cellIndex >= m_cells.size()) return;

    ImageCell &cell = m_cells[cellIndex];
    if (!cell.imageContainer) return;

    QRect r = cell.imageContainer->rect();
    cell.imageWidget->setGeometry(r);
    positionMarkButtonsForCell(cellIndex);

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

void ComparePanel::refreshCellsUsingFirstImage()
{
    if (!m_resizeToFirstImageEnabled ||
        m_cells.isEmpty() ||
        m_cells.first().originalImage.isNull()) {
        return;
    }

    for (int i = 1; i < m_cells.size(); ++i) {
        ImageCell &cell = m_cells[i];
        if (!cell.hasImage || cell.showingPreview) {
            continue;
        }

        cell.cachedToleranceImage = QImage();
        if (cell.showingToleranceMap && cell.toleranceSourceIndex >= 0) {
            showToleranceMap(cell.toleranceSourceIndex, i);
        } else {
            showOriginalImage(i, false);
        }
    }
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
    setComparisonThreshold(value);
}

void ComparePanel::onResizeToFirstImageToggled(bool enabled)
{
    setResizeToFirstImageEnabled(enabled);
}

void ComparePanel::renameCell(int cellIndex)
{
    if (cellIndex < 0 || cellIndex >= m_cells.size()) {
        return;
    }

    bool accepted = false;
    const QString newName = QInputDialog::getText(
        this,
        tr("Rename Comparison Grid"),
        tr("Grid name:"),
        QLineEdit::Normal,
        cellDisplayName(cellIndex),
        &accepted);

    if (!accepted) {
        return;
    }

    m_cells[cellIndex].customDisplayName = newName.trimmed();
    updateCellHeader(cellIndex);
    rebuildCompareButtons();
}

void ComparePanel::updateCellHeader(int cellIndex)
{
    if (cellIndex < 0 || cellIndex >= m_cells.size()) {
        return;
    }

    ImageCell &cell = m_cells[cellIndex];
    if (!cell.headerLabel) {
        return;
    }

    const QString displayName = cellDisplayName(cellIndex);
    if (cell.imagePath.isEmpty()) {
        cell.headerLabel->setText(
            QStringLiteral("%1\n%2").arg(tr("未选择图片"), displayName));
        cell.headerLabel->setToolTip(QStringLiteral("%1\n%2")
                                         .arg(displayName, cell.folderPath));
        return;
    }

    const QString fileName = QFileInfo(cell.imagePath).fileName();
    cell.headerLabel->setText(QStringLiteral("%1\n%2").arg(fileName, displayName));
    cell.headerLabel->setToolTip(QStringLiteral("%1\n%2\n%3")
                                     .arg(displayName, fileName, cell.imagePath));
}

QString ComparePanel::cellDisplayName(int cellIndex) const
{
    if (cellIndex < 0 || cellIndex >= m_cells.size()) {
        return QString();
    }

    const ImageCell &cell = m_cells[cellIndex];
    if (!cell.customDisplayName.isEmpty()) {
        return cell.customDisplayName;
    }

    const QString folderName = QDir(cell.folderPath).dirName();
    if (!folderName.isEmpty()) {
        return folderName;
    }

    return cell.folderPath;
}

QImage ComparePanel::imageForCompare(int cellIndex) const
{
    if (cellIndex < 0 || cellIndex >= m_cells.size()) return QImage();

    const QImage &original = m_cells[cellIndex].originalImage;
    const QImage &preview = m_cells[cellIndex].previewImage;
    const QImage baseImage = original.isNull() ? preview : original;
    if (baseImage.isNull()) return QImage();

    if (!m_resizeToFirstImageEnabled || cellIndex == 0 || m_cells.isEmpty()) {
        return baseImage;
    }

    const QImage &firstBaseImage = m_cells.first().originalImage;
    if (firstBaseImage.isNull()) {
        return baseImage;
    }

    if (baseImage.size() == firstBaseImage.size()) {
        return baseImage;
    }

    return baseImage.scaled(firstBaseImage.size(),
                            Qt::IgnoreAspectRatio,
                            Qt::SmoothTransformation);
}

void ComparePanel::showImageContextMenuForCell(QWidget *cellContainer,
                                               QWidget *sourceWidget,
                                               const QPoint &pos)
{
    if (!cellContainer || !sourceWidget) {
        return;
    }

    for (int i = 0; i < m_cells.size(); ++i) {
        if (m_cells[i].container != cellContainer) {
            continue;
        }

        ImageContextMenu::showMenu(m_cells[i].imagePath,
                                   sourceWidget->mapToGlobal(pos),
                                   this);
        return;
    }
}

void ComparePanel::startCellDrag(int cellIndex)
{
    if (cellIndex < 0 || cellIndex >= m_cells.size() || !m_cells[cellIndex].container) {
        return;
    }

    auto *mimeData = new QMimeData();
    mimeData->setData(QString::fromLatin1(kFolderOrderMimeType),
                      encodeFolderOrderDragIndex(cellIndex));

    auto *drag = new QDrag(this);
    drag->setMimeData(mimeData);

    QPixmap pixmap = m_cells[cellIndex].container->grab();
    if (!pixmap.isNull()) {
        const QSize maxDragPreviewSize(220, 160);
        if (pixmap.width() > maxDragPreviewSize.width() ||
            pixmap.height() > maxDragPreviewSize.height()) {
            pixmap = pixmap.scaled(maxDragPreviewSize,
                                   Qt::KeepAspectRatio,
                                   Qt::SmoothTransformation);
        }
        drag->setPixmap(pixmap);
        drag->setHotSpot(QPoint(pixmap.width() / 2, qMin(36, pixmap.height() / 2)));
    }

    m_cellDragSourceObject = nullptr;
    m_cellDragSourceIndex = -1;
    drag->exec(Qt::MoveAction);
}

int ComparePanel::findCellByDragObject(QObject *object) const
{
    for (int i = 0; i < m_cells.size(); ++i) {
        const ImageCell &cell = m_cells[i];
        if (object == cell.container ||
            object == cell.headerWidget ||
            object == cell.indexBadge ||
            object == cell.headerLabel ||
            object == cell.imageContainer ||
            object == cell.imageWidget) {
            return i;
        }
    }
    return -1;
}

bool ComparePanel::isCellDragHandle(QObject *object) const
{
    for (const ImageCell &cell : m_cells) {
        if (object == cell.container ||
            object == cell.headerWidget ||
            object == cell.indexBadge ||
            object == cell.headerLabel) {
            return true;
        }
    }
    return false;
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

    bool firstReferenceImageBecameAvailable = false;
    for (int i = 0; i < m_cells.size(); ++i) {
        if (m_cells[i].imagePath != imagePath) {
            continue;
        }

        const bool wasMissingOriginalImage = m_cells[i].originalImage.isNull();
        m_cells[i].originalImage = image;
        m_cells[i].hasImage = true;
        m_cells[i].showingPreview = false;
        m_cells[i].cachedToleranceImage = QImage();
        showOriginalImage(i, true);

        if (i == 0 && wasMissingOriginalImage) {
            firstReferenceImageBecameAvailable = true;
        }
    }

    if (firstReferenceImageBecameAvailable) {
        refreshCellsUsingFirstImage();
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

void ComparePanel::onMarkChanged(const QString &folderPath,
                                 const QString &imagePath,
                                 const QString &category)
{
    Q_UNUSED(category);

    const QString changedFolder = QDir::cleanPath(QFileInfo(folderPath).absoluteFilePath());
    const QString changedImage = QDir::cleanPath(QFileInfo(imagePath).absoluteFilePath());

    for (int i = 0; i < m_cells.size(); ++i) {
        const QString cellFolder = QDir::cleanPath(QFileInfo(m_cells[i].folderPath).absoluteFilePath());
        const QString cellImage = QDir::cleanPath(QFileInfo(m_cells[i].imagePath).absoluteFilePath());
        if (cellFolder == changedFolder && cellImage == changedImage) {
            updateMarkButtonsForCell(i);
        }
    }
}
