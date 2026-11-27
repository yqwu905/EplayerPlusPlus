#include "MainWindow.h"
#include "styles/FluentStyle.h"
#include "widgets/FolderPanel.h"
#include "widgets/BrowsePanel.h"
#include "widgets/ComparePanel.h"
#include "widgets/VlmAnnotationDialog.h"
#include "widgets/SettingsDialog.h"
#include "services/SettingsManager.h"
#include "services/ImageLoader.h"
#include "services/ImageMarkManager.h"
#include "services/CategoryExporter.h"
#include "models/CompareSession.h"

#include <QSplitter>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QApplication>
#include <QAction>
#include <QActionGroup>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QShortcut>
#include <QSizePolicy>
#include <QSlider>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

namespace
{

class CommandBarWidget final : public QWidget
{
public:
    explicit CommandBarWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
    }

    void setResizeCallback(std::function<void()> callback)
    {
        m_onResize = std::move(callback);
    }

    QSize minimumSizeHint() const override
    {
        return QSize(0, 44);
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        if (m_onResize) {
            m_onResize();
        }
    }

    void showEvent(QShowEvent *event) override
    {
        QWidget::showEvent(event);
        if (m_onResize) {
            m_onResize();
        }
    }

    void changeEvent(QEvent *event) override
    {
        QWidget::changeEvent(event);
        if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) {
            updateAfterStyleChange();
        }
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) {
            updateAfterStyleChange();
        }
        return QWidget::eventFilter(watched, event);
    }

    bool event(QEvent *event) override
    {
        const bool handled = QWidget::event(event);
        // QWidget handles Polish and child geometry updates (LayoutRequest) in
        // event() without forwarding them to changeEvent().
        if (event->type() == QEvent::Polish) {
            updateAfterStyleChange();
        } else if (event->type() == QEvent::LayoutRequest && isVisible() && m_onResize) {
            m_onResize();
        }
        return handled;
    }

private:
    void updateAfterStyleChange()
    {
        if (m_styleUpdateScheduled || !isVisible() || !m_onResize) {
            return;
        }
        m_styleUpdateScheduled = true;
        // The child controls can receive their style and font changes after the bar.
        QTimer::singleShot(0, this, [this]() {
            m_styleUpdateScheduled = false;
            if (isVisible() && m_onResize) {
                m_onResize();
            }
        });
    }

    std::function<void()> m_onResize;
    bool m_styleUpdateScheduled = false;
};

class CommandBarMeasureItem final : public QWidgetItem
{
public:
    using QWidgetItem::QWidgetItem;

    // Use Qt's constrained, style-adjusted hint even for hidden commands.
    bool isEmpty() const override { return false; }
};

constexpr int kSliderMinWidth = 88;
constexpr int kSliderPreferredWidth = 150;

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_settingsManager = new SettingsManager(this);
    m_compareSession = new CompareSession(this);
    m_imageLoader = new ImageLoader(this);
    m_imageMarkManager = new ImageMarkManager(this);

    // Push persisted color-profile policy into the loader before any decode
    // is requested, so the first thumbnail batch already uses the right key.
    m_imageLoader->setIgnoreColorProfile(m_settingsManager->ignoreImageColorProfile());

    setupUi();
    setupMenuBar();
    setupConnections();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi()
{
    setWindowTitle(tr("图像对比"));
    // Roomy enough that FolderPanel(280) + a usable BrowsePanel + ComparePanel(650)
    // plus splitter handles all fit without squeezing the compare area.
    setMinimumSize(1280, 800);
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

    // FolderPanel can be collapsed by dragging; BrowsePanel cannot — dragging its
    // handle to the edge just shrinks the thumbnails to their minimum and stops,
    // rather than collapsing the panel. The Browse panel is still fully hidable via
    // its toggle action (Ctrl+2), which hides the widget instead of collapsing it.
    m_mainSplitter->setCollapsible(0, true);
    m_mainSplitter->setCollapsible(1, false);
    m_mainSplitter->setCollapsible(2, false);

    // Only the compare panel grows on window resize; the folder rail and the
    // browse panel keep their user-set widths (the browse width is the thumbnail
    // zoom level, so it should stay where the user dragged it — and be restored
    // from settings across sessions).
    m_mainSplitter->setStretchFactor(0, 0);
    m_mainSplitter->setStretchFactor(1, 0);
    m_mainSplitter->setStretchFactor(2, 1);

    const QList<int> savedSizes = m_settingsManager->splitterSizes();
    int savedTotal = 0;
    for (int s : savedSizes) {
        savedTotal += s;
    }
    if (savedSizes.size() == 3 && savedTotal > 0) {
        m_mainSplitter->setSizes(savedSizes);
    } else {
        // Compact-ish browser, large compare area by default.
        m_mainSplitter->setSizes({280, 430, 970});
    }
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
        "QWidget#mainChrome { background: palette(window); }"
        "QSplitter#contentSplitter::handle { background: palette(window); border: none; }"
        "QWidget#commandBar { background: palette(base); border: 1px solid palette(mid); border-radius: 8px; }"
        "QFrame#commandSeparator, QFrame#optionsEndSeparator { background: palette(mid); border: none; max-width: 1px; min-width: 1px; }"
        "QToolButton#commandButton, QToolButton#commandOverflowButton { background: transparent; border: 1px solid transparent; border-radius: 6px; padding: 5px 8px; color: palette(window-text); font-size: 12px; }"
        "QToolButton#commandButton:hover, QToolButton#commandOverflowButton:hover { background: palette(light); border-color: palette(mid); }"
        "QToolButton#commandButton:pressed, QToolButton#commandOverflowButton:pressed { background: palette(midlight); }"
        "QToolButton#commandButton:checked { background: palette(midlight); border-color: palette(midlight); color: palette(link); font-weight: 600; }"
        "QToolButton#commandOverflowButton::menu-indicator { image: none; }"
        "QLabel#valuePill { background: palette(alternate-base); border: 1px solid palette(mid); border-radius: 5px; padding: 3px 6px; color: palette(window-text); font-size: 12px; font-weight: 600; }"
        "QLabel#toolbarLabel { color: palette(window-text); font-size: 12px; background: transparent; border: none; }");

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
        // Read the version from QApplication (set in main.cpp) instead of
        // hardcoding it here, where it had drifted to a stale v0.1.0.
        QMessageBox::about(this, tr("关于图像对比"),
            tr("图像对比 v%1\n\n基于 Qt 6 和 C++17 构建。")
                .arg(QApplication::applicationVersion()));
    });
    addAction(aboutAction);
}

QWidget *MainWindow::createCommandBar()
{
    auto *bar = new CommandBarWidget(this);
    bar->setObjectName(QStringLiteral("commandBar"));
    bar->setFixedHeight(44);
    m_commandBar = bar;
    m_optionsEndSeparator = nullptr;
    m_overflowCommandItems.clear();

    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(10, 4, 10, 4);
    layout->setSpacing(4);

    auto addSeparator = [layout, bar]() {
        auto *line = new QFrame(bar);
        line->setObjectName(QStringLiteral("commandSeparator"));
        line->setFixedSize(1, 22);
        layout->addWidget(line);
        return line;
    };

    auto addButton = [layout, bar, this](QAction *action,
                                         bool collapsible = false,
                                         bool inOptionsGroup = false) {
        auto *button = new QToolButton(bar);
        button->setObjectName(QStringLiteral("commandButton"));
        button->setDefaultAction(action);
        button->setIconSize(QSize(15, 15));
        button->setToolButtonStyle(action->icon().isNull()
                                       ? Qt::ToolButtonTextOnly
                                       : Qt::ToolButtonTextBesideIcon);
        button->setAutoRaise(false);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        layout->addWidget(button);
        if (collapsible) {
            button->installEventFilter(bar);
            m_overflowCommandItems.append(CommandOverflowItem{button, action, inOptionsGroup});
        }
        return button;
    };

    addButton(addCommandAction(FluentStyle::icon(FluentStyle::Icon::FolderAdd),
                               tr("添加"), tr("添加文件夹"),
                               m_folderPanel, SLOT(addFolderViaDialog())));
    addButton(addCommandAction(FluentStyle::icon(FluentStyle::Icon::Refresh),
                               tr("刷新"), tr("刷新文件夹"),
                               m_folderPanel, SLOT(refreshFolders())));
    addButton(addCommandAction(FluentStyle::icon(FluentStyle::Icon::Clear),
                               tr("清空"), tr("清空文件夹"),
                               m_folderPanel, SLOT(clearFolders())));

    addSeparator();

    m_swapModeAction = new QAction(FluentStyle::icon(FluentStyle::Icon::Swap), tr("交换"), bar);
    m_swapModeAction->setCheckable(true);
    m_swapModeAction->setChecked(true);
    m_toleranceModeAction = new QAction(FluentStyle::icon(FluentStyle::Icon::Tolerance), tr("容差图"), bar);
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

    m_reverseSwapDirectionAction = new QAction(tr("反向交换"), bar);
    m_reverseSwapDirectionAction->setCheckable(true);
    m_reverseSwapDirectionAction->setChecked(
        m_comparePanel ? m_comparePanel->reverseSwapDirectionEnabled() : false);
    m_reverseSwapDirectionAction->setToolTip(
        tr("开启后，按住交换按钮时将目标图片显示到当前图片；关闭时将当前图片显示到目标图片。"));
    connect(m_reverseSwapDirectionAction, &QAction::toggled,
            m_comparePanel, &ComparePanel::setReverseSwapDirectionEnabled);
    addButton(m_reverseSwapDirectionAction, /*collapsible=*/true);

    addSeparator();

    auto *thresholdLabel = new QLabel(tr("阈值"), bar);
    thresholdLabel->setObjectName(QStringLiteral("toolbarLabel"));
    thresholdLabel->setToolTip(tr("容差图像素差值阈值（0–255）"));
    layout->addWidget(thresholdLabel);

    m_thresholdSlider = new QSlider(Qt::Horizontal, bar);
    m_thresholdSlider->setRange(0, 255);
    m_thresholdSlider->setValue(m_comparePanel ? m_comparePanel->comparisonThreshold() : 10);
    m_thresholdSlider->setFixedWidth(kSliderPreferredWidth);
    m_thresholdSlider->setToolTip(tr("容差图像素差值阈值（0–255）"));
    layout->addWidget(m_thresholdSlider);

    m_thresholdValueLabel = new QLabel(QString::number(m_thresholdSlider->value()), bar);
    m_thresholdValueLabel->setObjectName(QStringLiteral("valuePill"));
    m_thresholdValueLabel->setAlignment(Qt::AlignCenter);
    m_thresholdValueLabel->setFixedWidth(42);
    m_thresholdValueLabel->setToolTip(tr("当前容差阈值（范围 0–255）"));
    layout->addWidget(m_thresholdValueLabel);

    connect(m_thresholdSlider, &QSlider::valueChanged, this, [this](int value) {
        if (m_thresholdValueLabel) {
            m_thresholdValueLabel->setText(QString::number(value));
        }
        m_comparePanel->setComparisonThreshold(value);
    });

    addSeparator();

    addButton(addCommandAction(FluentStyle::icon(FluentStyle::Icon::ArrowUp),
                               tr("上一张"), tr("上一张（向上方向键）"),
                               m_browsePanel, SLOT(navigatePrevious())));
    addButton(addCommandAction(FluentStyle::icon(FluentStyle::Icon::ArrowDown),
                               tr("下一张"), tr("下一张（向下方向键）"),
                               m_browsePanel, SLOT(navigateNext())));

    addSeparator();

    m_resizeToFirstAction = new QAction(tr("同步最大尺寸"), bar);
    m_resizeToFirstAction->setCheckable(true);
    m_resizeToFirstAction->setToolTip(
        tr("将所有对比图片平滑缩放到当前最大图片的尺寸"));
    m_resizeToFirstAction->setChecked(m_comparePanel ? m_comparePanel->resizeToFirstImageEnabled() : false);
    connect(m_resizeToFirstAction, &QAction::toggled,
            m_comparePanel, &ComparePanel::setResizeToFirstImageEnabled);
    addButton(m_resizeToFirstAction, /*collapsible=*/true, /*inOptionsGroup=*/true);

    m_imageNameOverlayAction = new QAction(tr("名称标注"), bar);
    m_imageNameOverlayAction->setCheckable(true);
    m_imageNameOverlayAction->setChecked(
        m_comparePanel ? m_comparePanel->imageNameOverlayEnabled() : false);
    m_imageNameOverlayAction->setToolTip(
        tr("开启后，在每张对比图片顶部用红色大字显示文件夹或自定义名称。"));
    connect(m_imageNameOverlayAction, &QAction::toggled,
            m_comparePanel, &ComparePanel::setImageNameOverlayEnabled);
    addButton(m_imageNameOverlayAction, /*collapsible=*/true, /*inOptionsGroup=*/true);

    m_copyComparisonImageAction = new QAction(tr("复制对比图"), bar);
    m_copyComparisonImageAction->setObjectName(QStringLiteral("copyComparisonImageAction"));
    m_copyComparisonImageAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_C));
    addAction(m_copyComparisonImageAction);
    m_copyComparisonImageAction->setToolTip(
        tr("生成对比拼接图并复制到剪贴板（Alt+C）"));
    connect(m_copyComparisonImageAction, &QAction::triggered,
            this, &MainWindow::copyComparisonImage);
    QToolButton *copyButton = addButton(m_copyComparisonImageAction,
                                        /*collapsible=*/true,
                                        /*inOptionsGroup=*/true);
    copyButton->setAccessibleName(QStringLiteral("复制对比图"));

    m_copyFeedbackTimer = new QTimer(this);
    m_copyFeedbackTimer->setSingleShot(true);
    connect(m_copyFeedbackTimer, &QTimer::timeout, this, [this]() {
        if (m_copyComparisonImageAction) {
            m_copyComparisonImageAction->setText(tr("复制对比图"));
            updateCommandBarOverflow();
        }
    });

    m_ignoreColorProfileAction = new QAction(tr("忽略 ICC"), bar);
    m_ignoreColorProfileAction->setCheckable(true);
    m_ignoreColorProfileAction->setChecked(m_settingsManager->ignoreImageColorProfile());
    m_ignoreColorProfileAction->setToolTip(
        tr("开启后，加载图片时丢弃嵌入的 ICC 色彩配置，使带不同色彩标签的同源图片按字节对比一致。"));
    connect(m_ignoreColorProfileAction, &QAction::toggled,
            this, [this](bool enabled) {
        m_settingsManager->setIgnoreImageColorProfile(enabled);
        m_imageLoader->setIgnoreColorProfile(enabled);
        // The loader cleared its caches; nudge the panels to re-decode what's
        // currently on screen so the new policy is visible without waiting
        // for a scroll or selection change.
        if (m_browsePanel) {
            m_browsePanel->refreshAllVisibleThumbnails();
        }
        if (m_comparePanel) {
            m_comparePanel->reloadAllImages();
        }
    });
    addButton(m_ignoreColorProfileAction, /*collapsible=*/true, /*inOptionsGroup=*/true);

    auto *fullScreenAction = new QAction(tr("全屏看图"), this);
    fullScreenAction->setCheckable(true);
    fullScreenAction->setAutoRepeat(false);
    fullScreenAction->setShortcut(QKeySequence(Qt::Key_F11));
    fullScreenAction->setToolTip(tr("全屏看图（F11，Esc 退出）"));
    addAction(fullScreenAction);
    connect(fullScreenAction, &QAction::toggled, this, &MainWindow::setImageFullScreen);
    addButton(fullScreenAction, true, true);

    m_exitFullScreenShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    m_exitFullScreenShortcut->setEnabled(false);
    connect(m_exitFullScreenShortcut, &QShortcut::activated, this, [fullScreenAction]() {
        fullScreenAction->setChecked(false);
    });

    auto *magnifierAction = new QAction(tr("放大镜"), this);
    magnifierAction->setCheckable(true);
    magnifierAction->setAutoRepeat(false);
    magnifierAction->setShortcut(QKeySequence(Qt::Key_F8));
    magnifierAction->setToolTip(tr("同步放大镜（F8）。Alt+滚轮调整倍率，Ctrl+滚轮调整方框"));
    addAction(magnifierAction);
    connect(magnifierAction, &QAction::toggled, m_comparePanel, &ComparePanel::setMagnifierEnabled);
    addButton(magnifierAction, true, true);

    auto *imageInformationAction = new QAction(tr("图像信息"), this);
    imageInformationAction->setCheckable(true);
    imageInformationAction->setToolTip(tr("在图片右上角显示 EXIF，在左下角显示 RGB 直方图"));
    connect(imageInformationAction, &QAction::toggled,
            m_comparePanel, &ComparePanel::setImageInformationVisible);
    addButton(imageInformationAction, true, true);

    auto *darkAppearanceAction = new QAction(tr("深色外观"), this);
    darkAppearanceAction->setObjectName(QStringLiteral("darkAppearanceAction"));
    darkAppearanceAction->setCheckable(true);
    darkAppearanceAction->setChecked(m_settingsManager->darkAppearance());
    darkAppearanceAction->setToolTip(tr("在深色与浅色外观之间切换"));
    connect(darkAppearanceAction, &QAction::toggled, this, [this](bool dark) {
        m_settingsManager->setDarkAppearance(dark);
        FluentStyle::applyGlobalStyle(qApp, dark);
    });
    addButton(darkAppearanceAction, true, true);

    auto *fuzzyMatchAction = new QAction(tr("模糊匹配"), bar);
    fuzzyMatchAction->setCheckable(true);
    fuzzyMatchAction->setChecked(false);
    fuzzyMatchAction->setToolTip(
        tr("开启后，Alt+点击会按最接近的文件名匹配其他对比文件夹中的图片。"));
    connect(fuzzyMatchAction, &QAction::toggled,
            m_browsePanel, &BrowsePanel::setFuzzyFileNameMatchEnabled);
    addButton(fuzzyMatchAction, /*collapsible=*/true, /*inOptionsGroup=*/true);

    m_optionsEndSeparator = addSeparator();
    m_optionsEndSeparator->setObjectName(QStringLiteral("optionsEndSeparator"));

    m_settingsAction = new QAction(FluentStyle::icon(FluentStyle::Icon::Settings), tr("设置"), bar);
    m_settingsAction->setToolTip(tr("打开全局设置"));
    connect(m_settingsAction, &QAction::triggered,
            this, &MainWindow::showSettingsDialog);
    addButton(m_settingsAction);

    m_vlmAnnotationAction = new QAction(FluentStyle::icon(FluentStyle::Icon::Sparkle), tr("AI 标注"), bar);
    m_vlmAnnotationAction->setToolTip(tr("使用 OpenAI 兼容 VLM API 批量标注当前过滤结果"));
    connect(m_vlmAnnotationAction, &QAction::triggered,
            this, &MainWindow::showVlmAnnotationDialog);
    addButton(m_vlmAnnotationAction);

    layout->addStretch();

    m_commandOverflowMenu = new QMenu(bar);
    m_commandOverflowMenu->setObjectName(QStringLiteral("commandOverflowMenu"));
    m_commandOverflowButton = new QToolButton(bar);
    m_commandOverflowButton->setObjectName(QStringLiteral("commandOverflowButton"));
    m_commandOverflowButton->setIcon(FluentStyle::icon(FluentStyle::Icon::More));
    m_commandOverflowButton->setIconSize(QSize(15, 15));
    m_commandOverflowButton->setToolTip(tr("更多命令选项"));
    m_commandOverflowButton->setPopupMode(QToolButton::InstantPopup);
    m_commandOverflowButton->setMenu(m_commandOverflowMenu);
    m_commandOverflowButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_commandOverflowButton->hide();
    layout->addWidget(m_commandOverflowButton);

    bar->setResizeCallback([this]() { updateCommandBarOverflow(); });

    return bar;
}

void MainWindow::updateCommandBarOverflow()
{
    if (m_updatingCommandBarOverflow || !m_commandBar || !m_thresholdSlider
        || !m_commandOverflowButton || !m_commandOverflowMenu) {
        return;
    }
    const QScopedValueRollback updating(m_updatingCommandBarOverflow, true);

    auto *layout = qobject_cast<QHBoxLayout *>(m_commandBar->layout());
    if (!layout) {
        return;
    }

    const int barWidth = m_commandBar->contentsRect().width();
    if (barWidth <= 0) {
        return;
    }

    auto itemWidthWithSpacing = [layout](QWidget *widget) {
        return CommandBarMeasureItem(widget).sizeHint().width() + layout->spacing();
    };

    auto syncOverflowMenu = [this](const QList<QAction *> &nextActions) {
        const bool anyOverflowed = !nextActions.isEmpty();
        if (!anyOverflowed && m_commandOverflowMenu->isVisible()) {
            m_commandOverflowMenu->close();
        }
        if (m_commandOverflowMenu->actions() != nextActions) {
            QAction *activeAction = m_commandOverflowMenu->activeAction();
            m_commandOverflowMenu->clear();
            m_commandOverflowMenu->addActions(nextActions);
            if (nextActions.contains(activeAction)) {
                m_commandOverflowMenu->setActiveAction(activeAction);
            }
        }
    };

    // Start with the live layout hint to include Qt's effective margins. This
    // bar has fixed spacing and always has nonempty items, so each added or
    // removed widget changes the hint by its layout item width plus one gap.
    layout->invalidate();
    int neededWidth = layout->sizeHint().width() + kSliderMinWidth - m_thresholdSlider->minimumWidth();
    for (const auto &item : std::as_const(m_overflowCommandItems)) {
        if (item.button->isHidden()) {
            neededWidth += itemWidthWithSpacing(item.button);
        }
    }
    const int optionsSeparatorWidth = itemWidthWithSpacing(m_optionsEndSeparator);
    if (m_optionsEndSeparator->isHidden()) {
        neededWidth += optionsSeparatorWidth;
    }
    const int overflowButtonWidth = itemWidthWithSpacing(m_commandOverflowButton);
    if (!m_commandOverflowButton->isHidden()) {
        neededWidth -= overflowButtonWidth;
    }

    qsizetype visibleCount = m_overflowCommandItems.size();
    auto visibleOptions = std::count_if(m_overflowCommandItems.cbegin(), m_overflowCommandItems.cend(),
                                        [](const auto &item) { return item.inOptionsGroup; });
    if (neededWidth > barWidth) {
        neededWidth += overflowButtonWidth;
        while (neededWidth > barWidth && visibleCount > 0) {
            const auto &item = m_overflowCommandItems.at(--visibleCount);
            neededWidth -= itemWidthWithSpacing(item.button);
            if (item.inOptionsGroup && --visibleOptions == 0) {
                neededWidth -= optionsSeparatorWidth;
            }
        }
    }

    const int sliderWidth =
        qBound(kSliderMinWidth, kSliderMinWidth + barWidth - neededWidth, kSliderPreferredWidth);
    QList<QAction *> overflowActions;
    for (qsizetype i = 0; i < m_overflowCommandItems.size(); ++i) {
        const auto &item = m_overflowCommandItems.at(i);
        item.button->setVisible(i < visibleCount);
        if (i >= visibleCount) {
            overflowActions.append(item.action);
        }
    }
    m_optionsEndSeparator->setVisible(visibleOptions > 0);
    syncOverflowMenu(overflowActions);
    m_commandOverflowButton->setVisible(!overflowActions.isEmpty());
    m_thresholdSlider->setFixedWidth(sliderWidth);
    layout->activate();
    // layout->invalidate() / setVisible() / setFixedWidth() post queued
    // LayoutRequest events back to m_commandBar; clear those stale requests
    // because layout->activate() just synchronized the bar geometry.
    QCoreApplication::removePostedEvents(m_commandBar, QEvent::LayoutRequest);
}

QAction *MainWindow::addCommandAction(const QIcon &icon,
                                      const QString &text,
                                      const QString &toolTip,
                                      const QObject *receiver,
                                      const char *member)
{
    auto *action = new QAction(icon, text, this);
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
    connect(m_comparePanel, &ComparePanel::reverseSwapDirectionChanged,
            this, [this](bool enabled) {
        if (m_reverseSwapDirectionAction &&
            m_reverseSwapDirectionAction->isChecked() != enabled) {
            m_reverseSwapDirectionAction->setChecked(enabled);
        }
    });
    connect(m_comparePanel, &ComparePanel::imageNameOverlayChanged,
            this, [this](bool enabled) {
        if (m_imageNameOverlayAction &&
            m_imageNameOverlayAction->isChecked() != enabled) {
            m_imageNameOverlayAction->setChecked(enabled);
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

void MainWindow::showVlmAnnotationDialog()
{
    if (!m_browsePanel || !m_imageMarkManager || !m_settingsManager) {
        return;
    }

    if (m_vlmAnnotationDialog) {
        m_vlmAnnotationDialog->showNormal();
        m_vlmAnnotationDialog->raise();
        m_vlmAnnotationDialog->activateWindow();
        return;
    }

    const auto snapshots = m_browsePanel->currentColumnSnapshots();
    m_vlmAnnotationDialog = new VlmAnnotationDialog(snapshots, m_settingsManager, m_imageMarkManager, this);
    m_vlmAnnotationDialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(m_vlmAnnotationDialog, &QObject::destroyed, this, [this]() {
        m_vlmAnnotationDialog = nullptr;
    });
    m_vlmAnnotationDialog->show();
    m_vlmAnnotationDialog->raise();
    m_vlmAnnotationDialog->activateWindow();
}

void MainWindow::showSettingsDialog()
{
    if (!m_settingsManager) {
        return;
    }

    SettingsDialog dialog(m_settingsManager, this);
    dialog.setCurrentTab(SettingsDialog::Tab::Vlm);
    dialog.exec();
}

void MainWindow::copyComparisonImage()
{
    if (!m_comparePanel) {
        return;
    }

    const bool copied = m_comparePanel->copyComparisonImageToClipboard();
    if (m_copyComparisonImageAction) {
        m_copyComparisonImageAction->setText(
            copied ? tr("✓ 已复制") : tr("无可复制图片"));
        updateCommandBarOverflow();
        if (m_copyFeedbackTimer) {
            m_copyFeedbackTimer->start(1500);
        }
    }
    if (m_imageFullScreen) {
        return;
    }
    statusBar()->showMessage(
        copied ? tr("已复制对比图到剪贴板") : tr("当前没有可复制的对比图片"),
        3000);
}

void MainWindow::setImageFullScreen(bool enabled)
{
    m_imageFullScreen = enabled;
    m_exitFullScreenShortcut->setEnabled(enabled);

    if (enabled) {
        m_restoreMaximized = isMaximized();
        m_windowedMinimumSize = minimumSize();
        m_windowedSplitterSizes = m_mainSplitter->sizes();
        m_windowedFocus = focusWidget();
        m_restoreFolderPanel = !m_folderPanel->isHidden();
        m_restoreBrowsePanel = !m_browsePanel->isHidden();
        m_commandBar->hide();
        m_folderPanel->hide();
        m_browsePanel->hide();
        m_comparePanel->setMinimumWidth(0);
        setMinimumSize(0, 0);
        centralWidget()->layout()->setContentsMargins(0, 0, 0, 0);
        m_comparePanel->setImageOnly(true);
        showFullScreen();
        m_comparePanel->setFocus(Qt::ShortcutFocusReason);
        return;
    }

    centralWidget()->layout()->setContentsMargins(8, 8, 8, 8);
    m_comparePanel->setImageOnly(false);
    m_comparePanel->setMinimumWidth(650);
    setMinimumSize(m_windowedMinimumSize);
    m_commandBar->show();
    m_folderPanel->setVisible(m_restoreFolderPanel);
    m_browsePanel->setVisible(m_restoreBrowsePanel);
    if (m_restoreMaximized) {
        showMaximized();
    } else {
        showNormal();
    }
    m_mainSplitter->setSizes(m_windowedSplitterSizes);
    if (m_windowedFocus && m_windowedFocus->isVisible()) {
        m_windowedFocus->setFocus(Qt::ShortcutFocusReason);
    }
}

void MainWindow::togglePanel(int panelIndex)
{
    // In image-only mode the checkable actions update the panels' restored visibility.
    if (m_imageFullScreen) {
        if (panelIndex == 0) {
            m_restoreFolderPanel = m_toggleFolderPanelAction->isChecked();
            if (m_restoreFolderPanel && m_windowedSplitterSizes[0] == 0) {
                m_windowedSplitterSizes[0] = qMax(280, m_savedSplitterSizes.value(0));
            }
        } else {
            m_restoreBrowsePanel = m_toggleBrowsePanelAction->isChecked();
            if (m_restoreBrowsePanel && m_windowedSplitterSizes[1] == 0) {
                m_windowedSplitterSizes[1] = qMax(480, m_savedSplitterSizes.value(1));
            }
        }
        return;
    }
    QWidget *panel = (panelIndex == 0) ? static_cast<QWidget *>(m_folderPanel)
                   : (panelIndex == 1) ? static_cast<QWidget *>(m_browsePanel)
                                       : nullptr;

    QList<int> sizes = m_mainSplitter->sizes();
    const bool visible = panel ? !panel->isHidden() : (sizes.at(panelIndex) > 0);

    if (visible) {
        // Collapse: remember the current widths, then hide the widget. We hide it
        // (rather than setting its size to 0) because a non-collapsible pane clamps
        // a 0 size back to its minimum — hiding fully removes it and its handle.
        m_savedSplitterSizes = sizes;
        if (panel) {
            panel->hide();
        } else {
            sizes[panelIndex] = 0;
            m_mainSplitter->setSizes(sizes);
        }
    } else {
        // Expand: show the widget and restore its saved width (show() alone only
        // brings it back at its minimum width).
        int restoreSize = 0;
        if (panelIndex < m_savedSplitterSizes.size()) {
            restoreSize = m_savedSplitterSizes.at(panelIndex);
        }
        if (restoreSize <= 0) {
            restoreSize = (panelIndex == 0) ? 240 : 480;
        }
        if (panel) {
            panel->show();
        }
        sizes = m_mainSplitter->sizes();
        sizes[panelIndex] = restoreSize;
        m_mainSplitter->setSizes(sizes);
    }

    // Update toggle action checked state
    if (panelIndex == 0 && m_toggleFolderPanelAction) {
        m_toggleFolderPanelAction->setChecked(m_folderPanel && !m_folderPanel->isHidden());
    } else if (panelIndex == 1 && m_toggleBrowsePanelAction) {
        m_toggleBrowsePanelAction->setChecked(m_browsePanel && !m_browsePanel->isHidden());
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
    // Persist so the browse-panel width (i.e. the thumbnail zoom level) is
    // restored on the next launch.
    if (m_settingsManager) {
        m_settingsManager->setSplitterSizes(m_savedSplitterSizes);
    }
}
