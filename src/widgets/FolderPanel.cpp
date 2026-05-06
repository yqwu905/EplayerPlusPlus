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
#include <QAbstractItemModel>
#include <QMessageBox>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QFileInfo>

namespace
{
const QColor kFolderColors[] = {
    QColor(0x18, 0x6F, 0xD7),
    QColor(0x22, 0x8A, 0x46),
    QColor(0xF7, 0x73, 0x13),
    QColor(0x74, 0x55, 0xC8),
    QColor(0x0F, 0x7B, 0x93),
    QColor(0xC5, 0x0F, 0x1F)
};
const int kFolderColorCount = sizeof(kFolderColors) / sizeof(kFolderColors[0]);

QIcon colorSquareIcon(int index)
{
    QPixmap pixmap(18, 18);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(kFolderColors[index % kFolderColorCount]);
    painter.drawRoundedRect(QRectF(2, 2, 14, 14), 4, 4);
    painter.setPen(QPen(Qt::white, 2));
    painter.drawLine(QPointF(6, 9), QPointF(8, 11));
    painter.drawLine(QPointF(8, 11), QPointF(12, 6));
    return QIcon(pixmap);
}
}

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
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 12);
    layout->setSpacing(10);

    setObjectName(QStringLiteral("folderPanelRoot"));
    setStyleSheet(
        "QWidget#folderPanelRoot { background-color: #FFFFFF; border: 1px solid #E3E7EC; border-radius: 8px; }"
        "QLabel#folderTitle { color: #111827; font-size: 15px; font-weight: 700; border: none; background: transparent; }"
        "QLabel#folderSubtitle { color: #6B7280; font-size: 11px; border: none; background: transparent; }"
        "QListWidget#rootFolderList { background: #FFFFFF; border: 1px solid #ECEFF3; border-radius: 8px; outline: none; padding: 4px; }"
        "QListWidget#rootFolderList::item { border-bottom: 1px solid #F0F2F5; border-radius: 6px; padding: 9px 8px; min-height: 40px; color: #1F2937; }"
        "QListWidget#rootFolderList::item:hover { background: #F7FAFD; }"
        "QListWidget#rootFolderList::item:selected { background: #EEF6FF; color: #111827; }"
        "QTreeView#folderTreeView { background-color: #FFFFFF; border: none; padding: 2px; outline: none; }"
        "QTreeView#folderTreeView::item { padding: 3px 6px; border-radius: 5px; min-height: 24px; color: #243041; }"
        "QTreeView#folderTreeView::item:hover { background-color: #F5F7FA; }"
        "QTreeView#folderTreeView::item:selected { background-color: #EAF4FF; color: #111827; }"
        "QLineEdit#folderPathInput { background-color: #F8FAFC; border: 1px solid #E5EAF1; border-radius: 6px; padding: 5px 8px; color: #374151; }"
        "QLineEdit#folderPathInput:focus { background-color: #FFFFFF; border-color: #2D7FF9; }"
        "QLabel#folderStatusLabel { color: #5E6A7A; font-size: 11px; border: none; background: transparent; }");

    auto *headerWidget = new QWidget(this);
    headerWidget->setFixedHeight(46);
    auto *headerLayout = new QVBoxLayout(headerWidget);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(3);

    auto *titleRow = new QHBoxLayout();
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(6);
    auto *titleLabel = new QLabel(tr("文件夹"), headerWidget);
    titleLabel->setObjectName(QStringLiteral("folderTitle"));
    titleRow->addWidget(titleLabel);
    titleRow->addStretch();
    auto *pinLabel = new QLabel(QStringLiteral("◇"), headerWidget);
    pinLabel->setStyleSheet("QLabel { color: #7C8797; font-size: 14px; border: none; background: transparent; }");
    titleRow->addWidget(pinLabel);
    headerLayout->addLayout(titleRow);

    auto *subtitleLabel = new QLabel(tr("最多 6 个文件夹"), headerWidget);
    subtitleLabel->setObjectName(QStringLiteral("folderSubtitle"));
    headerLayout->addWidget(subtitleLabel);
    headerLayout->addStretch();

    layout->addWidget(headerWidget);

    // ---- Toolbar retained for command actions, hidden visually in the new shell ----
    m_toolBar = new QToolBar(this);
    m_toolBar->setVisible(false);
    m_toolBar->setIconSize(QSize(16, 16));
    m_toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_toolBar->setStyleSheet(
        "QToolBar { background-color: #FAFAFA; border: none; "
        "border-bottom: 1px solid #E0E0E0; padding: 4px 8px; spacing: 4px; }");

    QAction *addAction = m_toolBar->addAction(tr("+ 添加"));
    addAction->setToolTip(tr("Add a folder to the list"));

    m_toolBar->addSeparator();

    QAction *refreshAction = m_toolBar->addAction(tr("↻ 刷新"));
    refreshAction->setToolTip(tr("Refresh all folders"));

    QAction *clearAction = m_toolBar->addAction(tr("✕ 清空"));
    clearAction->setToolTip(tr("Remove all folders"));

    connect(addAction, &QAction::triggered, this, &FolderPanel::addFolderViaDialog);
    connect(refreshAction, &QAction::triggered, this, &FolderPanel::refreshFolders);
    connect(clearAction, &QAction::triggered, this, &FolderPanel::clearFolders);

    // ---- Added roots list ----
    m_rootListWidget = new QListWidget(this);
    m_rootListWidget->setObjectName(QStringLiteral("rootFolderList"));
    m_rootListWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    m_rootListWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_rootListWidget->setUniformItemSizes(false);
    m_rootListWidget->setMaximumHeight(228);
    layout->addWidget(m_rootListWidget);

    // ---- Path Input ----
    m_pathInput = new QLineEdit(this);
    m_pathInput->setObjectName(QStringLiteral("folderPathInput"));
    m_pathInput->setPlaceholderText(tr("输入文件夹路径后按 Enter"));
    m_pathInput->setClearButtonEnabled(true);
    m_pathInput->setFixedHeight(30);
    layout->addWidget(m_pathInput);

    // ---- Tree View ----
    m_treeView = new QTreeView(this);
    m_treeView->setObjectName(QStringLiteral("folderTreeView"));
    m_treeView->setModel(m_folderModel);
    m_treeView->setHeaderHidden(true);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_treeView->setAnimated(true);
    m_treeView->setExpandsOnDoubleClick(true);
    m_treeView->setIndentation(20);
    m_treeView->setRootIsDecorated(true);

    layout->addWidget(m_treeView);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("folderStatusLabel"));
    layout->addWidget(m_statusLabel);
}

void FolderPanel::setupConnections()
{
    connect(m_treeView, &QTreeView::customContextMenuRequested,
            this, &FolderPanel::onContextMenu);
    connect(m_rootListWidget, &QListWidget::customContextMenuRequested,
            this, &FolderPanel::onRootListContextMenu);
    connect(m_rootListWidget, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem *item) {
        if (item) {
            emit addToCompareRequested(item->data(Qt::UserRole).toString());
        }
    });

    connect(m_folderModel, &FolderModel::addToCompareRequested,
            this, &FolderPanel::addToCompareRequested);
    connect(m_folderModel, &QAbstractItemModel::rowsInserted,
            this, &FolderPanel::syncRootList);
    connect(m_folderModel, &QAbstractItemModel::rowsRemoved,
            this, &FolderPanel::syncRootList);
    connect(m_folderModel, &QAbstractItemModel::modelReset,
            this, &FolderPanel::syncRootList);

    connect(m_pathInput, &QLineEdit::returnPressed,
            this, &FolderPanel::onPathSubmitted);
}

void FolderPanel::addFolderViaDialog()
{
    QString dir = QFileDialog::getExistingDirectory(
        this,
        tr("选择文件夹"),
        QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );

    if (dir.isEmpty()) {
        return;
    }

    if (m_folderModel->addFolder(dir)) {
        saveFolderList();
        syncRootList();
    }
}

void FolderPanel::refreshFolders()
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
        syncRootList();
        m_pathInput->clear();
    }
}

void FolderPanel::clearFolders()
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
        syncRootList();
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
            syncRootList();
        });
    }

    contextMenu.exec(m_treeView->viewport()->mapToGlobal(pos));
}

void FolderPanel::onRootListContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_rootListWidget->itemAt(pos);
    if (!item) {
        return;
    }

    const QString path = item->data(Qt::UserRole).toString();
    QMenu contextMenu(this);

    QAction *addToCompareAction = contextMenu.addAction(tr("添加到对比"));
    connect(addToCompareAction, &QAction::triggered, this, [this, path]() {
        emit addToCompareRequested(path);
    });

    QAction *refreshAction = contextMenu.addAction(tr("刷新"));
    connect(refreshAction, &QAction::triggered, this, [this, path]() {
        const QStringList roots = m_folderModel->rootFolderPaths();
        const int row = roots.indexOf(path);
        if (row >= 0) {
            m_folderModel->refreshFolder(m_folderModel->index(row, 0));
        }
    });

    QAction *deleteAction = contextMenu.addAction(tr("删除"));
    connect(deleteAction, &QAction::triggered, this, [this, path]() {
        const QStringList roots = m_folderModel->rootFolderPaths();
        const int row = roots.indexOf(path);
        if (row >= 0 && m_folderModel->removeFolder(m_folderModel->index(row, 0))) {
            saveFolderList();
            syncRootList();
        }
    });

    contextMenu.exec(m_rootListWidget->viewport()->mapToGlobal(pos));
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
        syncRootList();
    }
}

void FolderPanel::syncRootList()
{
    if (!m_rootListWidget || !m_statusLabel) {
        return;
    }

    m_rootListWidget->clear();
    const QStringList roots = m_folderModel->rootFolderPaths();
    for (int i = 0; i < roots.size(); ++i) {
        const QString path = roots.at(i);
        auto *item = new QListWidgetItem(colorSquareIcon(i), rootItemText(path), m_rootListWidget);
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
    }

    m_statusLabel->setText(tr("● 已选 %1 个文件夹").arg(roots.size()));
}

QString FolderPanel::rootItemText(const QString &path) const
{
    const QString name = QDir(path).dirName().isEmpty() ? path : QDir(path).dirName();
    return QStringLiteral("%1\n%2").arg(name, QDir::toNativeSeparators(path));
}
