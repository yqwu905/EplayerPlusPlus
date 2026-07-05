#include "SettingsDialog.h"
#include "VlmProviderSettingsDialog.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(SettingsManager *settingsManager,
                               QWidget *parent)
    : QDialog(parent)
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
                m_saveButton, &QPushButton::setEnabled);
    }
}

SettingsDialog::~SettingsDialog() = default;

void SettingsDialog::setCurrentTab(Tab tab)
{
    if (!m_tabWidget) {
        return;
    }
    m_tabWidget->setCurrentIndex(static_cast<int>(tab));
}

void SettingsDialog::saveAndAccept()
{
    if (m_vlmPage && !m_vlmPage->saveSettings()) {
        return;
    }
    accept();
}
