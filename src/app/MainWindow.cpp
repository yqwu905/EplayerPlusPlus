#include "MainWindow.h"
#include "widgets/FolderPanel.h"
#include "widgets/BrowsePanel.h"
#include "widgets/ComparePanel.h"
#include "services/SettingsManager.h"
#include "services/ImageLoader.h"
#include "models/CompareSession.h"

#include <QSplitter>
#include <QMenuBar>
#include <QMessageBox>
#include <QApplication>
#include <QAction>
#include <QStatusBar>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_settingsManager = new SettingsManager(this);
    m_compareSession = new CompareSession(this);
    m_imageLoader = new ImageLoader(this);

    setupUi();
    setupMenuBar();
    setupConnections();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi()
{
    setWindowTitle(tr("ImageCompare"));
    setMinimumSize(1200, 800);

    // ---- Main splitter: three panels side by side ----
    m_mainSplitter = new QSplitter(Qt::Horizontal, this);
    m_mainSplitter->setHandleWidth(1);

    // Left panel — Folder management
    m_folderPanel = new FolderPanel(m_settingsManager, m_mainSplitter);

    // Center panel — Image browsing
    m_browsePanel = new BrowsePanel(m_compareSession, m_imageLoader, m_mainSplitter);

    // Right panel — Image comparison
    m_comparePanel = new ComparePanel(m_compareSession, m_settingsManager, m_mainSplitter);
    m_comparePanel->setMinimumWidth(300);

    // Add panels to splitter
    m_mainSplitter->addWidget(m_folderPanel);
    m_mainSplitter->addWidget(m_browsePanel);
    m_mainSplitter->addWidget(m_comparePanel);

    // Enable collapsible for FolderPanel and BrowsePanel
    m_mainSplitter->setCollapsible(0, true);
    m_mainSplitter->setCollapsible(1, true);
    m_mainSplitter->setCollapsible(2, false);

    // Set initial size ratios: folder(1) : browse(1) : compare(3)
    m_mainSplitter->setStretchFactor(0, 1);
    m_mainSplitter->setStretchFactor(1, 1);
    m_mainSplitter->setStretchFactor(2, 3);
    m_mainSplitter->setSizes({240, 240, 720});
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

    setCentralWidget(m_mainSplitter);
}

void MainWindow::setupMenuBar()
{
    // ---- File menu ----
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));

    QAction *exitAction = fileMenu->addAction(tr("E&xit"));
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, qApp, &QApplication::quit);

    // ---- View menu ----
    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));

    m_toggleFolderPanelAction = viewMenu->addAction(tr("Folder Panel"));
    m_toggleFolderPanelAction->setCheckable(true);
    m_toggleFolderPanelAction->setChecked(true);
    m_toggleFolderPanelAction->setShortcut(QKeySequence(tr("Ctrl+1")));
    connect(m_toggleFolderPanelAction, &QAction::triggered, this, [this]() {
        togglePanel(0);
    });

    m_toggleBrowsePanelAction = viewMenu->addAction(tr("Browse Panel"));
    m_toggleBrowsePanelAction->setCheckable(true);
    m_toggleBrowsePanelAction->setChecked(true);
    m_toggleBrowsePanelAction->setShortcut(QKeySequence(tr("Ctrl+2")));
    connect(m_toggleBrowsePanelAction, &QAction::triggered, this, [this]() {
        togglePanel(1);
    });

    // ---- Help menu ----
    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));

    QAction *aboutAction = helpMenu->addAction(tr("&About"));
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, tr("About ImageCompare"),
            tr("ImageCompare v0.1.0\n\n"
               "A cross-platform image comparison tool.\n"
               "Built with Qt 6 and C++17."));
    });

    QAction *aboutQtAction = helpMenu->addAction(tr("About &Qt"));
    connect(aboutQtAction, &QAction::triggered, qApp, &QApplication::aboutQt);
}

void MainWindow::setupConnections()
{
    // FolderPanel "add to compare" → CompareSession
    connect(m_folderPanel, &FolderPanel::addToCompareRequested,
            m_compareSession, &CompareSession::addFolder);

    // BrowsePanel selection changes → ComparePanel
    connect(m_browsePanel, &BrowsePanel::selectionChanged,
            m_comparePanel, &ComparePanel::setSelectedImages);

    // ComparePanel navigation → BrowsePanel
    connect(m_comparePanel, &ComparePanel::navigatePreviousRequested,
            m_browsePanel, &BrowsePanel::navigatePrevious);
    connect(m_comparePanel, &ComparePanel::navigateNextRequested,
            m_browsePanel, &BrowsePanel::navigateNext);

    connect(m_browsePanel, &BrowsePanel::scanStatusChanged,
            this, [this](const QString &statusText) {
        statusBar()->showMessage(statusText);
    });
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
