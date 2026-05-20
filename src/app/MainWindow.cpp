#include "MainWindow.h"
#include "widgets/FolderPanel.h"
#include "widgets/BrowsePanel.h"
#include "widgets/ComparePanel.h"
#include "services/SettingsManager.h"
#include "services/ImageLoader.h"
#include "services/ImageMarkManager.h"
#include "services/CategoryExporter.h"
#include "models/CompareSession.h"

#include <QSplitter>
#include <QMenuBar>
#include <QMessageBox>
#include <QApplication>
#include <QAction>
#include <QActionGroup>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QPushButton>
#include <QSaveFile>
#include <QShortcut>
#include <QSlider>
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_settingsManager = new SettingsManager(this);
    m_compareSession = new CompareSession(this);
    m_imageLoader = new ImageLoader(this);
    m_imageMarkManager = new ImageMarkManager(this);

    setupUi();
    setupMenuBar();
    setupConnections();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi()
{
    setWindowTitle(tr("图像对比"));
    setMinimumSize(1200, 800);
    resize(1680, 940);
    statusBar()->hide();

    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("mainChrome"));
    auto *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(8, 8, 8, 8);
    rootLayout->setSpacing(8);

    // ---- Main splitter: three panels side by side ----
    m_mainSplitter = new QSplitter(Qt::Horizontal, central);
    m_mainSplitter->setHandleWidth(8);
    m_mainSplitter->setObjectName(QStringLiteral("contentSplitter"));

    // Left panel — Folder management
    m_folderPanel = new FolderPanel(m_settingsManager, m_mainSplitter);
    m_folderPanel->setMinimumWidth(280);

    // Center panel — Image browsing
    m_browsePanel = new BrowsePanel(m_compareSession, m_imageLoader, m_mainSplitter);
    m_browsePanel->setImageMarkManager(m_imageMarkManager);

    // Right panel — Image comparison
    m_comparePanel = new ComparePanel(m_compareSession, m_settingsManager, m_imageLoader, m_mainSplitter);
    m_comparePanel->setImageMarkManager(m_imageMarkManager);
    m_comparePanel->setControlsVisible(false);
    m_comparePanel->setMinimumWidth(650);

    rootLayout->addWidget(createCommandBar());

    // Add panels to splitter
    m_mainSplitter->addWidget(m_folderPanel);
    m_mainSplitter->addWidget(m_browsePanel);
    m_mainSplitter->addWidget(m_comparePanel);

    // Enable collapsible for FolderPanel and BrowsePanel
    m_mainSplitter->setCollapsible(0, true);
    m_mainSplitter->setCollapsible(1, true);
    m_mainSplitter->setCollapsible(2, false);

    // Match the reference layout: slim folder rail, dense browser, large compare area.
    m_mainSplitter->setStretchFactor(0, 1);
    m_mainSplitter->setStretchFactor(1, 2);
    m_mainSplitter->setStretchFactor(2, 3);
    m_mainSplitter->setSizes({280, 700, 700});
    m_savedSplitterSizes = m_mainSplitter->sizes();

    // Track splitter moves to keep toggle actions in sync
    connect(m_mainSplitter, &QSplitter::splitterMoved,
            this, [this](int /*pos*/, int /*index*/) {
        // Update toggle action checked state when user drags a handle
        if (m_toggleFolderPanelAction) {
            bool folderVisible = m_mainSplitter->sizes().at(0) > 0;
            m_toggleFolderPanelAction->setChecked(folderVisible);
        }
        if (m_toggleBrowsePanelAction) {
            bool browseVisible = m_mainSplitter->sizes().at(1) > 0;
            m_toggleBrowsePanelAction->setChecked(browseVisible);
        }
        // Save sizes whenever a non-collapsed state is present
        saveSplitterSizes();
    });

    rootLayout->addWidget(m_mainSplitter, 1);

    central->setStyleSheet(
        "QWidget#mainChrome { background: #F3F6FA; }"
        "QSplitter#contentSplitter::handle { background: #F3F6FA; border: none; }"
        "QWidget#commandBar { background: #FFFFFF; border: 1px solid #E3E7EC; border-radius: 8px; }"
        "QFrame#commandSeparator { background: #E3E7EC; border: none; max-width: 1px; min-width: 1px; }"
        "QToolButton#commandButton { background: transparent; border: 1px solid transparent; border-radius: 6px; padding: 6px 10px; color: #1F2937; font-size: 12px; }"
        "QToolButton#commandButton:hover { background: #F4F7FB; border-color: #E2E8F0; }"
        "QToolButton#commandButton:pressed { background: #CCE4F7; }"
        "QToolButton#commandButton:checked { background: #E5F1FB; border-color: #C7E0F4; color: #0078D4; font-weight: 600; }"
        "QPushButton#valuePill { background: #FFFFFF; border: 1px solid #DDE4EE; border-radius: 5px; padding: 4px 10px; color: #243041; font-size: 12px; }"
        "QLabel#toolbarLabel { color: #263241; font-size: 12px; background: transparent; border: none; }");

    setCentralWidget(central);
}

void MainWindow::setupMenuBar()
{
    menuBar()->hide();

    auto *exitAction = new QAction(tr("E&xit"), this);
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, qApp, &QApplication::quit);

    m_toggleFolderPanelAction = new QAction(tr("Folder Panel"), this);
    m_toggleFolderPanelAction->setCheckable(true);
    m_toggleFolderPanelAction->setChecked(true);
    m_toggleFolderPanelAction->setShortcut(QKeySequence(tr("Ctrl+1")));
    addAction(m_toggleFolderPanelAction);
    connect(m_toggleFolderPanelAction, &QAction::triggered, this, [this]() {
        togglePanel(0);
    });

    m_toggleBrowsePanelAction = new QAction(tr("Browse Panel"), this);
    m_toggleBrowsePanelAction->setCheckable(true);
    m_toggleBrowsePanelAction->setChecked(true);
    m_toggleBrowsePanelAction->setShortcut(QKeySequence(tr("Ctrl+2")));
    addAction(m_toggleBrowsePanelAction);
    connect(m_toggleBrowsePanelAction, &QAction::triggered, this, [this]() {
        togglePanel(1);
    });

    auto *aboutAction = new QAction(tr("&About"), this);
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, tr("关于图像对比"),
            tr("图像对比 v0.1.0\n\n基于 Qt 6 和 C++17 构建。"));
    });
    addAction(aboutAction);
}

QWidget *MainWindow::createCommandBar()
{
    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("commandBar"));
    bar->setFixedHeight(44);

    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(14, 5, 10, 5);
    layout->setSpacing(8);

    auto separator = [bar]() {
        auto *line = new QFrame(bar);
        line->setObjectName(QStringLiteral("commandSeparator"));
        line->setFixedHeight(24);
        return line;
    };

    auto addButton = [layout, bar](QAction *action) {
        auto *button = new QToolButton(bar);
        button->setObjectName(QStringLiteral("commandButton"));
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setAutoRaise(false);
        layout->addWidget(button);
        return button;
    };

    addButton(addCommandAction(QStringLiteral("⊞  添加"), tr("添加文件夹"),
                               m_folderPanel, SLOT(addFolderViaDialog())));
    addButton(addCommandAction(QStringLiteral("⟳  刷新"), tr("刷新文件夹"),
                               m_folderPanel, SLOT(refreshFolders())));
    addButton(addCommandAction(QStringLiteral("⌫  清空"), tr("清空文件夹"),
                               m_folderPanel, SLOT(clearFolders())));

    m_swapModeAction = new QAction(QStringLiteral("交换"), bar);
    m_swapModeAction->setCheckable(true);
    m_swapModeAction->setChecked(true);
    m_toleranceModeAction = new QAction(QStringLiteral("容差图"), bar);
    m_toleranceModeAction->setCheckable(true);
    auto *modeGroup = new QActionGroup(bar);
    modeGroup->addAction(m_swapModeAction);
    modeGroup->addAction(m_toleranceModeAction);
    modeGroup->setExclusive(true);
    connect(m_swapModeAction, &QAction::triggered, this, [this]() {
        m_comparePanel->setCompareMode(ComparePanel::SwapMode);
    });
    connect(m_toleranceModeAction, &QAction::triggered, this, [this]() {
        m_comparePanel->setCompareMode(ComparePanel::ToleranceMode);
    });

    addButton(m_swapModeAction);
    addButton(m_toleranceModeAction);

    layout->addWidget(separator());

    auto *thresholdLabel = new QLabel(tr("阈值"), bar);
    thresholdLabel->setObjectName(QStringLiteral("toolbarLabel"));
    layout->addWidget(thresholdLabel);

    m_thresholdSlider = new QSlider(Qt::Horizontal, bar);
    m_thresholdSlider->setRange(0, 255);
    m_thresholdSlider->setValue(m_comparePanel ? m_comparePanel->comparisonThreshold() : 10);
    m_thresholdSlider->setFixedWidth(190);
    layout->addWidget(m_thresholdSlider);

    auto *valueButton = new QPushButton(QString::number(m_thresholdSlider->value()), bar);
    valueButton->setObjectName(QStringLiteral("valuePill"));
    valueButton->setFixedWidth(52);
    m_thresholdValueLabel = new QLabel(QString::number(m_thresholdSlider->value()), valueButton);
    m_thresholdValueLabel->hide();
    layout->addWidget(valueButton);
    auto *percentLabel = new QLabel(QStringLiteral("%"), bar);
    percentLabel->setObjectName(QStringLiteral("toolbarLabel"));
    layout->addWidget(percentLabel);

    connect(m_thresholdSlider, &QSlider::valueChanged, this, [this, valueButton](int value) {
        valueButton->setText(QString::number(value));
        if (m_thresholdValueLabel) {
            m_thresholdValueLabel->setText(QString::number(value));
        }
        m_comparePanel->setComparisonThreshold(value);
    });

    layout->addWidget(separator());

    addButton(addCommandAction(QStringLiteral("↑  上一张"), tr("上一张"),
                               m_browsePanel, SLOT(navigatePrevious())));
    addButton(addCommandAction(QStringLiteral("↓  下一张"), tr("下一张"),
                               m_browsePanel, SLOT(navigateNext())));

    m_resizeToFirstAction = new QAction(QStringLiteral("同步尺寸"), bar);
    m_resizeToFirstAction->setCheckable(true);
    m_resizeToFirstAction->setChecked(m_comparePanel ? m_comparePanel->resizeToFirstImageEnabled() : false);
    connect(m_resizeToFirstAction, &QAction::toggled,
            m_comparePanel, &ComparePanel::setResizeToFirstImageEnabled);
    addButton(m_resizeToFirstAction);

    layout->addStretch();

    return bar;
}

QAction *MainWindow::addCommandAction(const QString &text,
                                      const QString &toolTip,
                                      const QObject *receiver,
                                      const char *member)
{
    auto *action = new QAction(text, this);
    action->setToolTip(toolTip);
    connect(action, SIGNAL(triggered()), receiver, member);
    return action;
}

void MainWindow::setupConnections()
{
    // FolderPanel "add to compare" → CompareSession
    connect(m_folderPanel, &FolderPanel::addToCompareRequested,
            m_compareSession, &CompareSession::addFolder);

    // Export folder classification → save dialog (from both folder tree and
    // thumbnail-column blank-area context menus).
    connect(m_folderPanel, &FolderPanel::exportCategoriesRequested,
            this, &MainWindow::exportCategoriesForFolder);
    connect(m_browsePanel, &BrowsePanel::exportCategoriesRequested,
            this, &MainWindow::exportCategoriesForFolder);

    // BrowsePanel selection changes → ComparePanel
    connect(m_browsePanel, &BrowsePanel::selectionChanged,
            m_comparePanel, &ComparePanel::setSelectedImages);

    // ComparePanel navigation → BrowsePanel
    connect(m_comparePanel, &ComparePanel::navigatePreviousRequested,
            m_browsePanel, &BrowsePanel::navigatePrevious);
    connect(m_comparePanel, &ComparePanel::navigateNextRequested,
            m_browsePanel, &BrowsePanel::navigateNext);

    auto *previousShortcut = new QShortcut(QKeySequence(Qt::Key_Up), this);
    previousShortcut->setContext(Qt::WindowShortcut);
    connect(previousShortcut, &QShortcut::activated,
            m_browsePanel, &BrowsePanel::navigatePrevious);

    auto *nextShortcut = new QShortcut(QKeySequence(Qt::Key_Down), this);
    nextShortcut->setContext(Qt::WindowShortcut);
    connect(nextShortcut, &QShortcut::activated,
            m_browsePanel, &BrowsePanel::navigateNext);

    connect(m_browsePanel, &BrowsePanel::scanStatusChanged,
            this, [this](const QString &statusText) {
        statusBar()->showMessage(statusText);
    });

    connect(m_comparePanel, &ComparePanel::compareModeChanged,
            this, &MainWindow::updateCompareModeActions);
    connect(m_comparePanel, &ComparePanel::comparisonThresholdChanged,
            this, [this](int value) {
        if (m_thresholdSlider && m_thresholdSlider->value() != value) {
            m_thresholdSlider->setValue(value);
        }
    });
    connect(m_comparePanel, &ComparePanel::resizeToFirstImageChanged,
            this, [this](bool enabled) {
        if (m_resizeToFirstAction && m_resizeToFirstAction->isChecked() != enabled) {
            m_resizeToFirstAction->setChecked(enabled);
        }
    });
}

void MainWindow::exportCategoriesForFolder(const QString &folderPath)
{
    if (folderPath.isEmpty() || !m_imageMarkManager) {
        return;
    }

    m_imageMarkManager->loadFolder(folderPath);
    const QHash<QString, QString> marks = m_imageMarkManager->marksForFolder(folderPath);
    if (marks.isEmpty()) {
        QMessageBox::information(this, tr("导出分类"),
                                 tr("该文件夹暂无已分类的图片。"));
        return;
    }

    const QString defaultName = QFileInfo(folderPath).fileName() + tr("_分类") + QStringLiteral(".csv");
    const QString defaultPath = QDir(folderPath).filePath(defaultName);
    const QString filePath = QFileDialog::getSaveFileName(
        this, tr("导出分类"), defaultPath,
        tr("CSV (*.csv);;文本 (*.txt);;JSON (*.json)"));
    if (filePath.isEmpty()) {
        return;
    }

    const CategoryExporter::Format format =
        CategoryExporter::formatForSuffix(QFileInfo(filePath).suffix());
    const QString payload = CategoryExporter::serialize(marks, format);

    QByteArray bytes;
    if (format == CategoryExporter::Format::Csv) {
        // UTF-8 BOM so Excel renders Chinese filenames correctly.
        bytes.append("\xEF\xBB\xBF");
    }
    bytes.append(payload.toUtf8());

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()
        || !file.commit()) {
        QMessageBox::warning(this, tr("导出分类"),
                             tr("写入文件失败：%1").arg(filePath));
        return;
    }

    statusBar()->showMessage(tr("已导出 %1 条分类到 %2").arg(marks.size()).arg(filePath), 5000);
}

void MainWindow::togglePanel(int panelIndex)
{
    QList<int> sizes = m_mainSplitter->sizes();

    if (sizes.at(panelIndex) > 0) {
        // Collapse: save current sizes, then set this panel to 0
        m_savedSplitterSizes = sizes;
        sizes[panelIndex] = 0;
        m_mainSplitter->setSizes(sizes);
    } else {
        // Expand: restore saved size (or use a reasonable default)
        int restoreSize = 0;
        if (panelIndex < m_savedSplitterSizes.size()) {
            restoreSize = m_savedSplitterSizes.at(panelIndex);
        }
        if (restoreSize <= 0) {
            restoreSize = (panelIndex == 0) ? 240 : 480;
        }
        sizes[panelIndex] = restoreSize;
        m_mainSplitter->setSizes(sizes);
    }

    // Update toggle action checked state
    if (panelIndex == 0 && m_toggleFolderPanelAction) {
        m_toggleFolderPanelAction->setChecked(sizes.at(0) > 0);
    } else if (panelIndex == 1 && m_toggleBrowsePanelAction) {
        m_toggleBrowsePanelAction->setChecked(sizes.at(1) > 0);
    }
}

void MainWindow::updateCompareModeActions()
{
    if (!m_comparePanel || !m_swapModeAction || !m_toleranceModeAction) {
        return;
    }

    const bool tolerance = m_comparePanel->compareMode() == ComparePanel::ToleranceMode;
    m_swapModeAction->setChecked(!tolerance);
    m_toleranceModeAction->setChecked(tolerance);
}

void MainWindow::saveSplitterSizes()
{
    QList<int> sizes = m_mainSplitter->sizes();
    // Only save if at least one collapsible panel is visible
    for (int i = 0; i < sizes.size(); ++i) {
        if (sizes.at(i) > 0 && i < m_savedSplitterSizes.size()) {
            m_savedSplitterSizes[i] = sizes.at(i);
        }
    }
}
