#include "FolderPanel.h"
#include "models/FolderModel.h"
#include "services/SettingsManager.h"

#include <QTreeView>
#include <QToolBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>

FolderPanel::FolderPanel(SettingsManager *settingsManager, QWidget *parent)
    : QWidget(parent)
    , m_settingsManager(settingsManager)
{
    m_folderModel = new FolderModel(this);

    setupUi();
    setupConnections();
    restoreFolderList();
}

FolderPanel::~FolderPanel() = default;

void FolderPanel::setupUi()
{
    setObjectName(QStringLiteral("folderPanel"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ---- Panel header ----
    auto *headerWidget = new QWidget(this);
    headerWidget->setObjectName(QStringLiteral("panelHeader"));
    headerWidget->setFixedHeight(44);
    auto *headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(16, 0, 8, 0);
    headerLayout->setSpacing(4);

    auto *titleLabel = new QLabel(tr("Folders"), headerWidget);
    titleLabel->setObjectName(QStringLiteral("panelTitleLabel"));
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();

    layout->addWidget(headerWidget);

    // ---- Toolbar ----
    m_toolBar = new QToolBar(this);
    m_toolBar->setObjectName(QStringLiteral("panelToolBar"));
    m_toolBar->setIconSize(QSize(16, 16));
    m_toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    QAction *addAction = m_toolBar->addAction(tr("+ Add"));
    addAction->setToolTip(tr("Add a folder to the list"));

    m_toolBar->addSeparator();

    QAction *refreshAction = m_toolBar->addAction(tr("↻ Refresh"));
    refreshAction->setToolTip(tr("Refresh all folders"));

    QAction *clearAction = m_toolBar->addAction(tr("✕ Clear"));
    clearAction->setToolTip(tr("Remove all folders"));

    connect(addAction, &QAction::triggered, this, &FolderPanel::onAddFolder);
    connect(refreshAction, &QAction::triggered, this, &FolderPanel::onRefreshAll);
    connect(clearAction, &QAction::triggered, this, &FolderPanel::onClearAll);

    layout->addWidget(m_toolBar);

    // ---- Path Input ----
    m_pathInput = new QLineEdit(this);
    m_pathInput->setObjectName(QStringLiteral("folderPathInput"));
    m_pathInput->setPlaceholderText(tr("Enter folder path and press Enter"));
    m_pathInput->setClearButtonEnabled(true);
    layout->addWidget(m_pathInput);

    // ---- Tree View ----
    m_treeView = new QTreeView(this);
    m_treeView->setModel(m_folderModel);
    m_treeView->setHeaderHidden(true);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_treeView->setAnimated(true);
    m_treeView->setExpandsOnDoubleClick(true);
    m_treeView->setIndentation(20);
    m_treeView->setRootIsDecorated(true);

    layout->addWidget(m_treeView);
}

void FolderPanel::setupConnections()
{
    connect(m_treeView, &QTreeView::customContextMenuRequested,
            this, &FolderPanel::onContextMenu);

    connect(m_folderModel, &FolderModel::addToCompareRequested,
            this, &FolderPanel::addToCompareRequested);

    connect(m_pathInput, &QLineEdit::returnPressed,
            this, &FolderPanel::onPathSubmitted);
}

void FolderPanel::onAddFolder()
{
    QString dir = QFileDialog::getExistingDirectory(
        this,
        tr("Select Folder"),
        QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );

    if (dir.isEmpty()) {
        return;
    }

    if (m_folderModel->addFolder(dir)) {
        saveFolderList();
    }
}

void FolderPanel::onRefreshAll()
{
    m_folderModel->refreshAll();
}

void FolderPanel::onPathSubmitted()
{
    if (!m_pathInput) {
        return;
    }

    const QString inputPath = m_pathInput->text().trimmed();
    if (inputPath.isEmpty()) {
        return;
    }

    if (m_folderModel->addFolder(inputPath)) {
        saveFolderList();
        m_pathInput->clear();
    }
}

void FolderPanel::onClearAll()
{
    if (m_folderModel->rootFolderPaths().isEmpty()) {
        return;
    }

    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        tr("Clear All Folders"),
        tr("Are you sure you want to remove all folders?"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );

    if (reply == QMessageBox::Yes) {
        m_folderModel->clearAll();
        saveFolderList();
    }
}

void FolderPanel::onContextMenu(const QPoint &pos)
{
    QModelIndex index = m_treeView->indexAt(pos);
    if (!index.isValid()) {
        return;
    }

    QString path = m_folderModel->filePath(index);
    bool isRoot = m_folderModel->isRootFolder(index);

    QMenu contextMenu(this);

    // "Add to Compare" — available for any folder
    QAction *addToCompareAction = contextMenu.addAction(tr("Add to Compare"));
    connect(addToCompareAction, &QAction::triggered, this, [this, path]() {
        emit addToCompareRequested(path);
    });

    contextMenu.addSeparator();

    // "Refresh" — available for any folder
    QAction *refreshAction = contextMenu.addAction(tr("Refresh"));
    connect(refreshAction, &QAction::triggered, this, [this, index]() {
        m_folderModel->refreshFolder(index);
    });

    // "Delete" — only for root folders
    if (isRoot) {
        QAction *deleteAction = contextMenu.addAction(tr("Delete"));
        connect(deleteAction, &QAction::triggered, this, [this, index]() {
            m_folderModel->removeFolder(index);
            saveFolderList();
        });
    }

    contextMenu.exec(m_treeView->viewport()->mapToGlobal(pos));
}

void FolderPanel::saveFolderList()
{
    if (m_settingsManager) {
        m_settingsManager->saveFolderList(m_folderModel->rootFolderPaths());
    }
}

void FolderPanel::restoreFolderList()
{
    if (m_settingsManager) {
        QStringList folders = m_settingsManager->loadFolderList();
        m_folderModel->setRootFolders(folders);
    }
}
