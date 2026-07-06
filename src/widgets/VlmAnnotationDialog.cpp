#include "VlmAnnotationDialog.h"
#include "services/ImageMarkManager.h"
#include "services/SettingsManager.h"
#include "SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QVBoxLayout>
#include <utility>

namespace
{
QString csvCell(QString value)
{
    value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(value);
}
}

VlmAnnotationDialog::VlmAnnotationDialog(
    const QList<VlmAnnotationService::ColumnSnapshot> &columns,
    SettingsManager *settingsManager,
    ImageMarkManager *markManager,
    QWidget *parent)
    : QDialog(parent)
    , m_columns(columns)
    , m_settingsManager(settingsManager)
    , m_markManager(markManager)
    , m_service(new VlmAnnotationService(this))
{
    setupUi();
    populateColumns();
    loadSettings();
    syncReferenceListForTarget();
    updateRunButtonState();

    connect(m_service, &VlmAnnotationService::progressChanged,
            this, &VlmAnnotationDialog::onServiceProgress);
    connect(m_service, &VlmAnnotationService::itemSucceeded,
            this, &VlmAnnotationDialog::onItemSucceeded);
    connect(m_service, &VlmAnnotationService::itemStatusChanged,
            this, &VlmAnnotationDialog::onItemStatusChanged);
    connect(m_service, &VlmAnnotationService::itemFailed,
            this, &VlmAnnotationDialog::onItemFailed);
    connect(m_service, &VlmAnnotationService::finished,
            this, &VlmAnnotationDialog::onServiceFinished);
}

VlmAnnotationDialog::~VlmAnnotationDialog()
{
    if (m_service) {
        m_service->cancel();
    }
}

void VlmAnnotationDialog::setupUi()
{
    setWindowTitle(tr("AI 标注"));
    resize(920, 720);
    setModal(true);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto *form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(8);

    auto *providerRow = new QWidget(this);
    auto *providerLayout = new QHBoxLayout(providerRow);
    providerLayout->setContentsMargins(0, 0, 0, 0);
    providerLayout->setSpacing(8);
    m_providerCombo = new QComboBox(providerRow);
    m_providerCombo->setObjectName(QStringLiteral("vlmProviderCombo"));
    providerLayout->addWidget(m_providerCombo, 1);
    m_manageProvidersButton = new QPushButton(tr("管理"), providerRow);
    m_manageProvidersButton->setObjectName(QStringLiteral("vlmManageProvidersButton"));
    providerLayout->addWidget(m_manageProvidersButton);
    form->addRow(tr("VLM Provider"), providerRow);

    m_promptEdit = new QTextEdit(this);
    m_promptEdit->setObjectName(QStringLiteral("vlmPromptEdit"));
    m_promptEdit->setAcceptRichText(false);
    m_promptEdit->setPlaceholderText(tr("填写 A-F 的判定标准和标注要求"));
    m_promptEdit->setMinimumHeight(90);
    form->addRow(tr("标注要求"), m_promptEdit);

    root->addLayout(form);

    auto *columnsRow = new QHBoxLayout();
    columnsRow->setSpacing(12);

    auto *referenceGroup = new QVBoxLayout();
    auto *referenceLabel = new QLabel(tr("参考列"), this);
    m_referenceList = new QListWidget(this);
    m_referenceList->setObjectName(QStringLiteral("vlmReferenceList"));
    m_referenceList->setMinimumHeight(130);
    referenceGroup->addWidget(referenceLabel);
    referenceGroup->addWidget(m_referenceList);
    columnsRow->addLayout(referenceGroup, 1);

    auto *optionsForm = new QFormLayout();
    m_targetCombo = new QComboBox(this);
    m_targetCombo->setObjectName(QStringLiteral("vlmTargetCombo"));
    optionsForm->addRow(tr("分类列"), m_targetCombo);

    m_matchRuleCombo = new QComboBox(this);
    m_matchRuleCombo->setObjectName(QStringLiteral("vlmMatchRuleCombo"));
    m_matchRuleCombo->addItem(tr("按顺序"), VlmAnnotationService::matchRuleToInt(VlmAnnotationService::MatchRule::Order));
    m_matchRuleCombo->addItem(tr("按文件名"), VlmAnnotationService::matchRuleToInt(VlmAnnotationService::MatchRule::FileName));
    m_matchRuleCombo->addItem(tr("按文件名（模糊）"), VlmAnnotationService::matchRuleToInt(VlmAnnotationService::MatchRule::FileNameFuzzy));
    optionsForm->addRow(tr("匹配规则"), m_matchRuleCombo);

    m_maxItemsSpin = new QSpinBox(this);
    m_maxItemsSpin->setObjectName(QStringLiteral("vlmMaxItemsSpin"));
    m_maxItemsSpin->setRange(0, 1000000);
    m_maxItemsSpin->setSpecialValueText(tr("无限制"));
    optionsForm->addRow(tr("最大数量"), m_maxItemsSpin);

    m_concurrencySpin = new QSpinBox(this);
    m_concurrencySpin->setObjectName(QStringLiteral("vlmConcurrencySpin"));
    m_concurrencySpin->setRange(1, 16);
    optionsForm->addRow(tr("并发推理"), m_concurrencySpin);

    m_skipMarkedCheck = new QCheckBox(tr("跳过已有分类"), this);
    m_skipMarkedCheck->setObjectName(QStringLiteral("vlmSkipMarkedCheck"));
    optionsForm->addRow(QString(), m_skipMarkedCheck);
    columnsRow->addLayout(optionsForm, 1);

    root->addLayout(columnsRow);

    auto *progressRow = new QHBoxLayout();
    m_progressBar = new QProgressBar(this);
    m_progressBar->setObjectName(QStringLiteral("vlmProgressBar"));
    m_progressBar->setRange(0, 0);
    m_progressBar->setValue(0);
    m_statusLabel = new QLabel(tr("待运行"), this);
    m_statusLabel->setObjectName(QStringLiteral("vlmStatusLabel"));
    progressRow->addWidget(m_progressBar, 1);
    progressRow->addWidget(m_statusLabel);
    root->addLayout(progressRow);

    m_resultTable = new QTableWidget(this);
    m_resultTable->setObjectName(QStringLiteral("vlmResultTable"));
    m_resultTable->setColumnCount(5);
    m_resultTable->setHorizontalHeaderLabels({tr("ID"), tr("文件"), tr("分类"), tr("原因 / 状态"), tr("参考数")});
    m_resultTable->horizontalHeader()->setStretchLastSection(false);
    m_resultTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_resultTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_resultTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_resultTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_resultTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_resultTable->verticalHeader()->hide();
    m_resultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultTable->setWordWrap(true);
    m_resultTable->setTextElideMode(Qt::ElideNone);
    root->addWidget(m_resultTable, 1);

    auto *buttons = new QHBoxLayout();
    buttons->addStretch();
    m_runButton = new QPushButton(tr("运行"), this);
    m_runButton->setObjectName(QStringLiteral("vlmRunButton"));
    m_cancelButton = new QPushButton(tr("取消"), this);
    m_cancelButton->setObjectName(QStringLiteral("vlmCancelButton"));
    m_exportButton = new QPushButton(tr("导出报告"), this);
    m_exportButton->setObjectName(QStringLiteral("vlmExportButton"));
    m_closeButton = new QPushButton(tr("关闭"), this);
    m_closeButton->setObjectName(QStringLiteral("vlmCloseButton"));
    buttons->addWidget(m_runButton);
    buttons->addWidget(m_cancelButton);
    buttons->addWidget(m_exportButton);
    buttons->addWidget(m_closeButton);
    root->addLayout(buttons);

    m_cancelButton->setEnabled(false);
    m_exportButton->setEnabled(false);

    connect(m_providerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &VlmAnnotationDialog::updateRunButtonState);
    connect(m_manageProvidersButton, &QPushButton::clicked,
            this, &VlmAnnotationDialog::onManageProvidersClicked);
    connect(m_promptEdit, &QTextEdit::textChanged,
            this, &VlmAnnotationDialog::updateRunButtonState);
    connect(m_targetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &VlmAnnotationDialog::onTargetColumnChanged);
    connect(m_runButton, &QPushButton::clicked,
            this, &VlmAnnotationDialog::onRunClicked);
    connect(m_cancelButton, &QPushButton::clicked,
            this, &VlmAnnotationDialog::onCancelClicked);
    connect(m_exportButton, &QPushButton::clicked,
            this, &VlmAnnotationDialog::onExportClicked);
    connect(m_resultTable, &QTableWidget::itemDoubleClicked,
            this, &VlmAnnotationDialog::onResultItemDoubleClicked);
    connect(m_closeButton, &QPushButton::clicked,
            this, &QDialog::accept);
}

void VlmAnnotationDialog::populateColumns()
{
    m_targetCombo->clear();
    m_referenceList->clear();
    for (const auto &column : std::as_const(m_columns)) {
        const QString name = column.columnName.isEmpty()
            ? QFileInfo(column.folderPath).fileName()
            : column.columnName;
        const QString text = tr("#%1  %2（%3 张）")
                                 .arg(column.columnIndex + 1)
                                 .arg(name)
                                 .arg(column.images.size());
        m_targetCombo->addItem(text, column.columnIndex);

        auto *item = new QListWidgetItem(text, m_referenceList);
        item->setData(Qt::UserRole, column.columnIndex);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
    }
}

void VlmAnnotationDialog::populateProviders()
{
    if (!m_settingsManager || !m_providerCombo) {
        return;
    }

    const QString activeId = m_settingsManager->activeVlmProviderId();
    const QList<SettingsManager::VlmProvider> providers = m_settingsManager->vlmProviders();
    m_providerCombo->clear();
    for (const auto &provider : providers) {
        const QString label = provider.name.trimmed().isEmpty()
            ? provider.model
            : provider.name;
        m_providerCombo->addItem(label, provider.id);
        const int row = m_providerCombo->count() - 1;
        m_providerCombo->setItemData(row,
                                     tr("%1\n%2").arg(provider.baseUrl, provider.model),
                                     Qt::ToolTipRole);
    }

    const int activeRow = m_providerCombo->findData(activeId);
    if (activeRow >= 0) {
        m_providerCombo->setCurrentIndex(activeRow);
    }
}

void VlmAnnotationDialog::loadSettings()
{
    if (!m_settingsManager) {
        return;
    }

    populateProviders();
    m_promptEdit->setPlainText(m_settingsManager->vlmUserPrompt());
    m_matchRuleCombo->setCurrentIndex(qMax(0, m_matchRuleCombo->findData(m_settingsManager->vlmMatchRule())));
    m_maxItemsSpin->setValue(m_settingsManager->vlmMaxItems());
    m_concurrencySpin->setValue(m_settingsManager->vlmConcurrency());
    m_skipMarkedCheck->setChecked(m_settingsManager->vlmSkipMarked());
}

void VlmAnnotationDialog::saveSettings()
{
    if (!m_settingsManager) {
        return;
    }

    const QString providerId = m_providerCombo->currentData().toString();
    if (!providerId.isEmpty()) {
        m_settingsManager->setActiveVlmProviderId(providerId);
    }
    m_settingsManager->setVlmUserPrompt(m_promptEdit->toPlainText());
    m_settingsManager->setVlmMatchRule(m_matchRuleCombo->currentData().toInt());
    m_settingsManager->setVlmMaxItems(m_maxItemsSpin->value());
    m_settingsManager->setVlmConcurrency(m_concurrencySpin->value());
    m_settingsManager->setVlmSkipMarked(m_skipMarkedCheck->isChecked());
}

void VlmAnnotationDialog::syncReferenceListForTarget()
{
    const int target = selectedTargetColumn();
    for (int i = 0; i < m_referenceList->count(); ++i) {
        QListWidgetItem *item = m_referenceList->item(i);
        const bool isTarget = item->data(Qt::UserRole).toInt() == target;
        Qt::ItemFlags flags = item->flags() | Qt::ItemIsUserCheckable;
        if (isTarget) {
            flags &= ~Qt::ItemIsEnabled;
            item->setCheckState(Qt::Unchecked);
        } else {
            flags |= Qt::ItemIsEnabled;
            if (item->checkState() == Qt::Unchecked) {
                item->setCheckState(Qt::Checked);
            }
        }
        item->setFlags(flags);
    }
}

QList<int> VlmAnnotationDialog::selectedReferenceColumns() const
{
    QList<int> columns;
    for (int i = 0; i < m_referenceList->count(); ++i) {
        const QListWidgetItem *item = m_referenceList->item(i);
        if ((item->flags() & Qt::ItemIsEnabled) && item->checkState() == Qt::Checked) {
            columns.append(item->data(Qt::UserRole).toInt());
        }
    }
    return columns;
}

int VlmAnnotationDialog::selectedTargetColumn() const
{
    bool ok = false;
    const int value = m_targetCombo->currentData().toInt(&ok);
    return ok ? value : -1;
}

SettingsManager::VlmProvider VlmAnnotationDialog::selectedProvider() const
{
    if (!m_settingsManager || !m_providerCombo) {
        return {};
    }

    const QString providerId = m_providerCombo->currentData().toString();
    const QList<SettingsManager::VlmProvider> providers = m_settingsManager->vlmProviders();
    for (const auto &provider : providers) {
        if (provider.id == providerId) {
            return provider;
        }
    }
    return providers.isEmpty() ? SettingsManager::VlmProvider{} : providers.first();
}

VlmAnnotationService::MatchRule VlmAnnotationDialog::selectedMatchRule() const
{
    return VlmAnnotationService::matchRuleFromInt(m_matchRuleCombo->currentData().toInt());
}

VlmAnnotationService::ApiConfig VlmAnnotationDialog::apiConfig() const
{
    VlmAnnotationService::ApiConfig config;
    const SettingsManager::VlmProvider provider = selectedProvider();
    config.baseUrl = provider.baseUrl;
    config.model = provider.model;
    config.apiKey = provider.apiKey;
    config.userPrompt = m_promptEdit->toPlainText();
    config.batchSize = 4;
    config.concurrency = m_concurrencySpin->value();
    return config;
}

void VlmAnnotationDialog::setRunningState(bool running)
{
    m_providerCombo->setEnabled(!running);
    m_manageProvidersButton->setEnabled(!running);
    m_promptEdit->setEnabled(!running);
    m_referenceList->setEnabled(!running);
    m_targetCombo->setEnabled(!running);
    m_matchRuleCombo->setEnabled(!running);
    m_maxItemsSpin->setEnabled(!running);
    m_concurrencySpin->setEnabled(!running);
    m_skipMarkedCheck->setEnabled(!running);
    m_runButton->setEnabled(!running);
    m_cancelButton->setEnabled(running);
    m_closeButton->setEnabled(!running);
    if (!running) {
        updateRunButtonState();
    }
}

void VlmAnnotationDialog::setupResultsTable(const QList<VlmAnnotationService::Task> &tasks)
{
    m_resultTable->setRowCount(tasks.size());
    m_results.clear();
    m_results.resize(tasks.size());

    for (int row = 0; row < tasks.size(); ++row) {
        const auto &task = tasks.at(row);
        auto *idItem = new QTableWidgetItem(task.id);
        auto *fileItem = new QTableWidgetItem(task.target.fileName);
        fileItem->setToolTip(task.target.imagePath);
        const QString queuedStatus = tr("排队中");
        const QString queuedDetail = tr("等待调度；目标列=%1，参考图=%2 张，文件=%3")
                                         .arg(task.target.columnName)
                                         .arg(task.references.size())
                                         .arg(task.target.imagePath);
        auto *categoryItem = new QTableWidgetItem(queuedStatus);
        auto *reasonItem = new QTableWidgetItem(queuedDetail);
        categoryItem->setToolTip(queuedDetail);
        reasonItem->setToolTip(queuedDetail);
        auto *referenceItem = new QTableWidgetItem(QString::number(task.references.size()));
        m_resultTable->setItem(row, 0, idItem);
        m_resultTable->setItem(row, 1, fileItem);
        m_resultTable->setItem(row, 2, categoryItem);
        m_resultTable->setItem(row, 3, reasonItem);
        m_resultTable->setItem(row, 4, referenceItem);

        ResultRow result;
        result.id = task.id;
        result.fileName = task.target.fileName;
        result.status = queuedStatus;
        result.reason = queuedDetail;
        m_results[row] = result;
    }
}

void VlmAnnotationDialog::updateResultRow(int taskIndex, const ResultRow &result)
{
    if (taskIndex < 0 || taskIndex >= m_resultTable->rowCount()) {
        return;
    }
    m_results[taskIndex] = result;
    const QString categoryText = result.category.isEmpty()
        ? result.status
        : result.category;
    const QString detail = result.reason.isEmpty() ? result.status : result.reason;
    m_resultTable->item(taskIndex, 2)->setText(categoryText);
    m_resultTable->item(taskIndex, 2)->setToolTip(detail);
    m_resultTable->item(taskIndex, 3)->setText(detail);
    m_resultTable->item(taskIndex, 3)->setToolTip(detail);
    m_resultTable->resizeRowToContents(taskIndex);
}

QString VlmAnnotationDialog::exportAsCsv() const
{
    QStringList lines;
    lines << QStringLiteral("id,file,category,applied,reason,status");
    for (const ResultRow &result : m_results) {
        lines << QStringList({
            csvCell(result.id),
            csvCell(result.fileName),
            csvCell(result.category),
            result.applied ? QStringLiteral("true") : QStringLiteral("false"),
            csvCell(result.reason),
            csvCell(result.status)
        }).join(QLatin1Char(','));
    }
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString VlmAnnotationDialog::exportAsJson() const
{
    QJsonArray array;
    for (const ResultRow &result : m_results) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), result.id);
        object.insert(QStringLiteral("file"), result.fileName);
        object.insert(QStringLiteral("category"), result.category);
        object.insert(QStringLiteral("applied"), result.applied);
        object.insert(QStringLiteral("reason"), result.reason);
        object.insert(QStringLiteral("status"), result.status);
        array.append(object);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Indented));
}

void VlmAnnotationDialog::updateRunButtonState()
{
    const SettingsManager::VlmProvider provider = selectedProvider();
    const bool valid = !m_service->isRunning()
        && !m_columns.isEmpty()
        && selectedTargetColumn() >= 0
        && !provider.baseUrl.trimmed().isEmpty()
        && !provider.model.trimmed().isEmpty()
        && !m_promptEdit->toPlainText().trimmed().isEmpty();
    m_runButton->setEnabled(valid);
}

void VlmAnnotationDialog::onTargetColumnChanged()
{
    syncReferenceListForTarget();
    updateRunButtonState();
}

void VlmAnnotationDialog::onRunClicked()
{
    saveSettings();

    VlmAnnotationService::PlanningOptions options;
    options.referenceColumns = selectedReferenceColumns();
    options.targetColumn = selectedTargetColumn();
    options.matchRule = selectedMatchRule();
    options.maxItems = m_maxItemsSpin->value();
    options.skipMarked = m_skipMarkedCheck->isChecked();

    m_tasks = VlmAnnotationService::buildPlan(m_columns, options);
    if (m_tasks.isEmpty()) {
        QMessageBox::information(this, tr("AI 标注"), tr("没有需要标注的图片。"));
        return;
    }

    setupResultsTable(m_tasks);
    m_progressBar->setRange(0, m_tasks.size());
    m_progressBar->setValue(0);
    m_statusLabel->setText(tr("开始 AI 标注"));
    m_exportButton->setEnabled(false);
    setRunningState(true);
    m_service->start(apiConfig(), m_tasks);
}

void VlmAnnotationDialog::onCancelClicked()
{
    if (m_service) {
        m_service->cancel();
    }
}

void VlmAnnotationDialog::onExportClicked()
{
    if (m_results.isEmpty()) {
        return;
    }

    const QString defaultPath = QDir::home().filePath(tr("AI标注结果.csv"));
    const QString filePath = QFileDialog::getSaveFileName(
        this, tr("导出 AI 标注报告"), defaultPath,
        tr("CSV (*.csv);;JSON (*.json)"));
    if (filePath.isEmpty()) {
        return;
    }

    const bool json = QFileInfo(filePath).suffix().compare(QStringLiteral("json"), Qt::CaseInsensitive) == 0;
    const QString payload = json ? exportAsJson() : exportAsCsv();
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(payload.toUtf8()) != payload.toUtf8().size() ||
        !file.commit()) {
        QMessageBox::warning(this, tr("导出 AI 标注报告"),
                             tr("写入文件失败：%1").arg(filePath));
    }
}

void VlmAnnotationDialog::onManageProvidersClicked()
{
    if (!m_settingsManager || m_service->isRunning()) {
        return;
    }

    SettingsDialog dialog(m_settingsManager, this);
    dialog.setCurrentTab(SettingsDialog::Tab::Vlm);
    if (dialog.exec() == QDialog::Accepted) {
        populateProviders();
        updateRunButtonState();
    }
}

void VlmAnnotationDialog::onResultItemDoubleClicked(QTableWidgetItem *item)
{
    if (!item || item->column() != 3) {
        return;
    }
    const QString text = item->text().trimmed();
    if (text.isEmpty()) {
        return;
    }
    const QString fileName = m_resultTable->item(item->row(), 1)
        ? m_resultTable->item(item->row(), 1)->text()
        : QString();
    QMessageBox::information(this,
                             tr("分类原因"),
                             fileName.isEmpty() ? text : tr("%1\n\n%2").arg(fileName, text));
}

void VlmAnnotationDialog::onServiceProgress(int completed,
                                            int total,
                                            const QString &statusText)
{
    m_progressBar->setRange(0, total);
    m_progressBar->setValue(completed);
    m_statusLabel->setText(statusText);
}

void VlmAnnotationDialog::onItemSucceeded(int taskIndex,
                                          const QString &id,
                                          const QString &category,
                                          const QString &reason)
{
    ResultRow result;
    result.id = id;
    result.category = category;
    result.reason = reason;
    result.status = tr("已写入");

    if (taskIndex >= 0 && taskIndex < m_tasks.size()) {
        result.fileName = m_tasks.at(taskIndex).target.fileName;
        result.applied = VlmAnnotationService::applyAcceptedResult(m_markManager,
                                                                   m_tasks.at(taskIndex),
                                                                   category,
                                                                   reason);
    }
    if (!result.applied) {
        result.status = tr("写入失败");
        result.reason = result.reason.isEmpty() ? result.status : result.reason;
    }
    updateResultRow(taskIndex, result);
}

void VlmAnnotationDialog::onItemStatusChanged(int taskIndex,
                                              const QString &id,
                                              const QString &statusText)
{
    if (taskIndex < 0 || taskIndex >= m_results.size()) {
        return;
    }

    ResultRow result = m_results.at(taskIndex);
    if (!result.category.isEmpty()) {
        return;
    }

    result.id = id;
    result.status = statusText;
    result.reason = statusText;
    if (result.fileName.isEmpty() && taskIndex < m_tasks.size()) {
        result.fileName = m_tasks.at(taskIndex).target.fileName;
    }
    updateResultRow(taskIndex, result);
}

void VlmAnnotationDialog::onItemFailed(int taskIndex,
                                       const QString &id,
                                       const QString &error)
{
    ResultRow result;
    result.id = id;
    if (taskIndex >= 0 && taskIndex < m_tasks.size()) {
        result.fileName = m_tasks.at(taskIndex).target.fileName;
    }
    result.category = tr("失败");
    result.status = error;
    result.reason = error;
    result.applied = false;
    updateResultRow(taskIndex, result);
}

void VlmAnnotationDialog::onServiceFinished(bool cancelled)
{
    setRunningState(false);
    m_exportButton->setEnabled(!m_results.isEmpty());
    m_statusLabel->setText(cancelled ? tr("已取消") : tr("AI 标注完成"));
}
