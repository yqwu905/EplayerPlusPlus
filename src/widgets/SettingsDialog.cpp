#include "SettingsDialog.h"
#include "VlmProviderSettingsDialog.h"
#include "services/SettingsManager.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(SettingsManager *settingsManager,
                               QWidget *parent)
    : QDialog(parent)
    , m_settingsManager(settingsManager)
{
    setObjectName(QStringLiteral("settingsDialog"));
    setWindowTitle(tr("设置"));
    resize(780, 520);
    setModal(true);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setObjectName(QStringLiteral("settingsTabWidget"));
    m_vlmPage = new VlmProviderSettingsDialog(settingsManager, m_tabWidget, true);
    m_vlmPage->setObjectName(QStringLiteral("settingsVlmPage"));
    m_tabWidget->addTab(m_vlmPage, tr("VLM"));

    m_performancePage = new QWidget(m_tabWidget);
    m_performancePage->setObjectName(QStringLiteral("settingsPerformancePage"));
    auto *performanceLayout = new QVBoxLayout(m_performancePage);
    performanceLayout->setContentsMargins(16, 16, 16, 16);
    performanceLayout->setSpacing(12);

    auto *networkGroup = new QGroupBox(tr("Windows 网络文件夹"), m_performancePage);
    auto *networkLayout = new QVBoxLayout(networkGroup);
    networkLayout->setSpacing(10);

    m_windowsNetworkOptimizationsCheck = new QCheckBox(
        tr("启用 Windows 网络文件夹优化（实验性）"), networkGroup);
    m_windowsNetworkOptimizationsCheck->setObjectName(
        QStringLiteral("windowsNetworkOptimizationsCheckBox"));
    m_windowsNetworkOptimizationsCheck->setChecked(
        settingsManager && settingsManager->windowsNetworkOptimizationsEnabled());
    networkLayout->addWidget(m_windowsNetworkOptimizationsCheck);

    auto *description = new QLabel(
        tr("仅作用于 UNC 路径和 Windows 映射网络盘。本总开关默认关闭，"
           "可在同一版本中切换传统模式与优化模式进行 A/B 对比。"), networkGroup);
    description->setObjectName(QStringLiteral("windowsNetworkOptimizationDescription"));
    description->setWordWrap(true);
    networkLayout->addWidget(description);

    m_windowsNetworkFastEnumerationCheck = new QCheckBox(
        tr("使用原生目录枚举并复用文件时间（减少 SMB 元数据往返）"), networkGroup);
    m_windowsNetworkFastEnumerationCheck->setObjectName(
        QStringLiteral("windowsNetworkFastEnumerationCheckBox"));
    m_windowsNetworkFastEnumerationCheck->setChecked(
        !settingsManager || settingsManager->windowsNetworkFastEnumerationEnabled());
    networkLayout->addWidget(m_windowsNetworkFastEnumerationCheck);

    m_windowsNetworkConstrainedPrefetchCheck = new QCheckBox(
        tr("限制网络缩略图预取范围与并发（优先显示当前可见图片）"), networkGroup);
    m_windowsNetworkConstrainedPrefetchCheck->setObjectName(
        QStringLiteral("windowsNetworkConstrainedPrefetchCheckBox"));
    m_windowsNetworkConstrainedPrefetchCheck->setChecked(
        !settingsManager || settingsManager->windowsNetworkConstrainedPrefetchEnabled());
    networkLayout->addWidget(m_windowsNetworkConstrainedPrefetchCheck);

    m_windowsNetworkDeferredValidationCheck = new QCheckBox(
        tr("启动时延后验证网络目录（避免离线共享阻塞界面）"), networkGroup);
    m_windowsNetworkDeferredValidationCheck->setObjectName(
        QStringLiteral("windowsNetworkDeferredValidationCheckBox"));
    m_windowsNetworkDeferredValidationCheck->setChecked(
        !settingsManager || settingsManager->windowsNetworkDeferredFolderValidationEnabled());
    networkLayout->addWidget(m_windowsNetworkDeferredValidationCheck);

    performanceLayout->addWidget(networkGroup);
    performanceLayout->addStretch();
    m_tabWidget->addTab(m_performancePage, tr("性能"));
    root->addWidget(m_tabWidget, 1);

    auto *buttons = new QHBoxLayout();
    buttons->addStretch();
    m_saveButton = new QPushButton(tr("保存"), this);
    m_saveButton->setObjectName(QStringLiteral("settingsSaveButton"));
    m_cancelButton = new QPushButton(tr("取消"), this);
    m_cancelButton->setObjectName(QStringLiteral("settingsCancelButton"));
    m_saveButton->setEnabled(m_vlmPage ? m_vlmPage->settingsValid() : true);
    buttons->addWidget(m_saveButton);
    buttons->addWidget(m_cancelButton);
    root->addLayout(buttons);

    connect(m_saveButton, &QPushButton::clicked,
            this, &SettingsDialog::saveAndAccept);
    connect(m_cancelButton, &QPushButton::clicked,
            this, &QDialog::reject);
    if (m_vlmPage) {
        connect(m_vlmPage, &VlmProviderSettingsDialog::settingsValidityChanged,
                this, &SettingsDialog::updateSaveButtonState);
    }
    connect(m_tabWidget, &QTabWidget::currentChanged,
            this, &SettingsDialog::updateSaveButtonState);
    connect(m_windowsNetworkOptimizationsCheck, &QCheckBox::toggled,
            this, &SettingsDialog::updatePerformanceControls);
    updatePerformanceControls(m_windowsNetworkOptimizationsCheck->isChecked());
    updateSaveButtonState();
}

SettingsDialog::~SettingsDialog() = default;

void SettingsDialog::setCurrentTab(Tab tab)
{
    if (!m_tabWidget) {
        return;
    }
    m_tabWidget->setCurrentIndex(static_cast<int>(tab));
    updateSaveButtonState();
}

void SettingsDialog::saveAndAccept()
{
    const bool editingVlm = m_tabWidget &&
        m_tabWidget->currentWidget() == m_vlmPage;
    if (m_vlmPage) {
        if (m_vlmPage->settingsValid()) {
            if (!m_vlmPage->saveSettings()) {
                return;
            }
        } else if (editingVlm) {
            return;
        }
    }

    if (m_settingsManager) {
        m_settingsManager->setWindowsNetworkOptimizationsEnabled(
            m_windowsNetworkOptimizationsCheck &&
            m_windowsNetworkOptimizationsCheck->isChecked());
        m_settingsManager->setWindowsNetworkFastEnumerationEnabled(
            m_windowsNetworkFastEnumerationCheck &&
            m_windowsNetworkFastEnumerationCheck->isChecked());
        m_settingsManager->setWindowsNetworkConstrainedPrefetchEnabled(
            m_windowsNetworkConstrainedPrefetchCheck &&
            m_windowsNetworkConstrainedPrefetchCheck->isChecked());
        m_settingsManager->setWindowsNetworkDeferredFolderValidationEnabled(
            m_windowsNetworkDeferredValidationCheck &&
            m_windowsNetworkDeferredValidationCheck->isChecked());
    }
    accept();
}

void SettingsDialog::updateSaveButtonState()
{
    if (!m_saveButton || !m_tabWidget) {
        return;
    }
    const bool editingVlm = m_tabWidget->currentWidget() == m_vlmPage;
    m_saveButton->setEnabled(!editingVlm || !m_vlmPage || m_vlmPage->settingsValid());
}

void SettingsDialog::updatePerformanceControls(bool enabled)
{
    if (m_windowsNetworkFastEnumerationCheck) {
        m_windowsNetworkFastEnumerationCheck->setEnabled(enabled);
    }
    if (m_windowsNetworkConstrainedPrefetchCheck) {
        m_windowsNetworkConstrainedPrefetchCheck->setEnabled(enabled);
    }
    if (m_windowsNetworkDeferredValidationCheck) {
        m_windowsNetworkDeferredValidationCheck->setEnabled(enabled);
    }
}
