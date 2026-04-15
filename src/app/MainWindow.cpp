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
    m_folderPanel->setMinimumWidth(200);

    // Center panel — Image browsing
    m_browsePanel = new BrowsePanel(m_compareSession, m_imageLoader, m_mainSplitter);
    m_browsePanel->setMinimumWidth(300);

    // Right panel — Image comparison
    m_comparePanel = new ComparePanel(m_compareSession, m_settingsManager, m_mainSplitter);
    m_comparePanel->setMinimumWidth(300);

    // Add panels to splitter
    m_mainSplitter->addWidget(m_folderPanel);
    m_mainSplitter->addWidget(m_browsePanel);
    m_mainSplitter->addWidget(m_comparePanel);

    // Set initial size ratios: folder(1) : browse(2) : compare(2)
    m_mainSplitter->setStretchFactor(0, 1);
    m_mainSplitter->setStretchFactor(1, 2);
    m_mainSplitter->setStretchFactor(2, 2);
    m_mainSplitter->setSizes({240, 480, 480});

    setCentralWidget(m_mainSplitter);
}

void MainWindow::setupMenuBar()
{
    // ---- File menu ----
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));

    QAction *exitAction = fileMenu->addAction(tr("E&xit"));
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, qApp, &QApplication::quit);

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
}
