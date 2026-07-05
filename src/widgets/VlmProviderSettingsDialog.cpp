#include "VlmProviderSettingsDialog.h"

#include <QBuffer>
#include <QByteArray>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkReply>
#include <QPushButton>
#include <QUuid>
#include <QVBoxLayout>
#include <utility>

VlmProviderSettingsDialog::VlmProviderSettingsDialog(SettingsManager *settingsManager,
                                                     QWidget *parent,
                                                     bool embedded)
    : QDialog(parent)
    , m_settingsManager(settingsManager)
    , m_embedded(embedded)
{
    setupUi();
    loadProviders();
    refreshProviderList();
    updateButtonState();
}

VlmProviderSettingsDialog::~VlmProviderSettingsDialog()
{
    if (m_testReply) {
        m_testReply->abort();
    }
}

void VlmProviderSettingsDialog::setupUi()
{
    if (m_embedded) {
        setWindowFlags(Qt::Widget);
    } else {
        setWindowTitle(tr("VLM Provider 设置"));
        resize(720, 420);
        setModal(true);
    }

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(m_embedded ? 0 : 16,
                             m_embedded ? 0 : 16,
                             m_embedded ? 0 : 16,
                             m_embedded ? 0 : 16);
    root->setSpacing(12);

    auto *mainRow = new QHBoxLayout();
    mainRow->setSpacing(12);

    auto *leftColumn = new QVBoxLayout();
    auto *listLabel = new QLabel(tr("Provider"), this);
    m_providerList = new QListWidget(this);
    m_providerList->setObjectName(QStringLiteral("vlmProviderList"));
    m_providerList->setMinimumWidth(220);
    leftColumn->addWidget(listLabel);
    leftColumn->addWidget(m_providerList, 1);

    auto *listButtons = new QHBoxLayout();
    m_addButton = new QPushButton(tr("添加"), this);
    m_addButton->setObjectName(QStringLiteral("vlmProviderAddButton"));
    m_deleteButton = new QPushButton(tr("删除"), this);
    m_deleteButton->setObjectName(QStringLiteral("vlmProviderDeleteButton"));
    listButtons->addWidget(m_addButton);
    listButtons->addWidget(m_deleteButton);
    leftColumn->addLayout(listButtons);
    mainRow->addLayout(leftColumn, 0);

    auto *form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(10);

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setObjectName(QStringLiteral("vlmProviderNameEdit"));
    m_nameEdit->setPlaceholderText(tr("例如 OpenAI / 本地 Qwen"));
    form->addRow(tr("名称"), m_nameEdit);

    m_baseUrlEdit = new QLineEdit(this);
    m_baseUrlEdit->setObjectName(QStringLiteral("vlmProviderBaseUrlEdit"));
    m_baseUrlEdit->setPlaceholderText(QStringLiteral("https://api.openai.com/v1"));
    form->addRow(tr("API Base URL"), m_baseUrlEdit);

    m_modelEdit = new QLineEdit(this);
    m_modelEdit->setObjectName(QStringLiteral("vlmProviderModelEdit"));
    m_modelEdit->setPlaceholderText(tr("模型名称"));
    form->addRow(tr("Model"), m_modelEdit);

    m_apiKeyEdit = new QLineEdit(this);
    m_apiKeyEdit->setObjectName(QStringLiteral("vlmProviderApiKeyEdit"));
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_apiKeyEdit->setPlaceholderText(tr("API Key"));
    form->addRow(tr("API Key"), m_apiKeyEdit);

    auto *rightColumn = new QVBoxLayout();
    rightColumn->addLayout(form);
    m_hintLabel = new QLabel(tr("API Key 会以明文保存在本机 QSettings 中。"), this);
    m_hintLabel->setObjectName(QStringLiteral("vlmProviderHintLabel"));
    m_hintLabel->setWordWrap(true);
    rightColumn->addWidget(m_hintLabel);

    auto *testRow = new QHBoxLayout();
    m_testButton = new QPushButton(tr("测试图片输入"), this);
    m_testButton->setObjectName(QStringLiteral("vlmProviderTestButton"));
    m_testStatusLabel = new QLabel(tr("未测试"), this);
    m_testStatusLabel->setObjectName(QStringLiteral("vlmProviderTestStatusLabel"));
    m_testStatusLabel->setWordWrap(true);
    testRow->addWidget(m_testButton);
    testRow->addWidget(m_testStatusLabel, 1);
    rightColumn->addLayout(testRow);

    rightColumn->addStretch();
    mainRow->addLayout(rightColumn, 1);

    root->addLayout(mainRow, 1);

    if (!m_embedded) {
        auto *buttons = new QHBoxLayout();
        buttons->addStretch();
        m_saveButton = new QPushButton(tr("保存"), this);
        m_saveButton->setObjectName(QStringLiteral("vlmProviderSaveButton"));
        m_cancelButton = new QPushButton(tr("取消"), this);
        m_cancelButton->setObjectName(QStringLiteral("vlmProviderCancelButton"));
        buttons->addWidget(m_saveButton);
        buttons->addWidget(m_cancelButton);
        root->addLayout(buttons);
    }

    connect(m_providerList, &QListWidget::currentRowChanged,
            this, &VlmProviderSettingsDialog::onProviderSelectionChanged);
    connect(m_nameEdit, &QLineEdit::textChanged,
            this, &VlmProviderSettingsDialog::onFieldChanged);
    connect(m_baseUrlEdit, &QLineEdit::textChanged,
            this, &VlmProviderSettingsDialog::onFieldChanged);
    connect(m_modelEdit, &QLineEdit::textChanged,
            this, &VlmProviderSettingsDialog::onFieldChanged);
    connect(m_apiKeyEdit, &QLineEdit::textChanged,
            this, &VlmProviderSettingsDialog::onFieldChanged);
    connect(m_addButton, &QPushButton::clicked,
            this, &VlmProviderSettingsDialog::addProvider);
    connect(m_deleteButton, &QPushButton::clicked,
            this, &VlmProviderSettingsDialog::deleteProvider);
    connect(m_testButton, &QPushButton::clicked,
            this, &VlmProviderSettingsDialog::testProviderImageInput);
    if (m_saveButton) {
        connect(m_saveButton, &QPushButton::clicked,
                this, &VlmProviderSettingsDialog::saveAndAccept);
    }
    if (m_cancelButton) {
        connect(m_cancelButton, &QPushButton::clicked,
                this, &QDialog::reject);
    }
}

void VlmProviderSettingsDialog::loadProviders()
{
    m_providers = m_settingsManager ? m_settingsManager->vlmProviders() : QList<SettingsManager::VlmProvider>{};
    if (m_providers.isEmpty()) {
        m_providers.append(defaultProvider());
    }
}

void VlmProviderSettingsDialog::refreshProviderList()
{
    m_syncing = true;
    m_providerList->clear();
    const QString activeId = m_settingsManager ? m_settingsManager->activeVlmProviderId() : QString();
    int activeRow = 0;
    for (int i = 0; i < m_providers.size(); ++i) {
        const auto &provider = m_providers.at(i);
        auto *item = new QListWidgetItem(provider.name, m_providerList);
        item->setData(Qt::UserRole, provider.id);
        if (provider.id == activeId) {
            activeRow = i;
        }
    }
    m_syncing = false;

    if (!m_providers.isEmpty()) {
        m_providerList->setCurrentRow(qBound(0, activeRow, m_providers.size() - 1));
    } else {
        populateFields(-1);
    }
}

void VlmProviderSettingsDialog::populateFields(int index)
{
    m_syncing = true;
    m_currentIndex = index;
    const bool valid = index >= 0 && index < m_providers.size();
    setFieldsEnabled(valid);
    if (valid) {
        const auto &provider = m_providers.at(index);
        m_nameEdit->setText(provider.name);
        m_baseUrlEdit->setText(provider.baseUrl);
        m_modelEdit->setText(provider.model);
        m_apiKeyEdit->setText(provider.apiKey);
    } else {
        m_nameEdit->clear();
        m_baseUrlEdit->clear();
        m_modelEdit->clear();
        m_apiKeyEdit->clear();
    }
    m_syncing = false;
    updateButtonState();
}

void VlmProviderSettingsDialog::updateSelectedProviderFromFields()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_providers.size()) {
        return;
    }

    auto &provider = m_providers[m_currentIndex];
    provider.name = m_nameEdit->text().trimmed();
    provider.baseUrl = m_baseUrlEdit->text().trimmed();
    provider.model = m_modelEdit->text().trimmed();
    provider.apiKey = m_apiKeyEdit->text();

    if (QListWidgetItem *item = m_providerList->item(m_currentIndex)) {
        item->setText(provider.name.isEmpty() ? tr("未命名 Provider") : provider.name);
    }
}

void VlmProviderSettingsDialog::setFieldsEnabled(bool enabled)
{
    m_nameEdit->setEnabled(enabled);
    m_baseUrlEdit->setEnabled(enabled);
    m_modelEdit->setEnabled(enabled);
    m_apiKeyEdit->setEnabled(enabled);
}

void VlmProviderSettingsDialog::updateButtonState()
{
    bool hasInvalid = m_providers.isEmpty();
    for (const auto &provider : std::as_const(m_providers)) {
        if (provider.name.trimmed().isEmpty() ||
            provider.baseUrl.trimmed().isEmpty() ||
            provider.model.trimmed().isEmpty()) {
            hasInvalid = true;
            break;
        }
    }
    const bool validSettings = !hasInvalid;
    if (m_settingsValid != validSettings) {
        m_settingsValid = validSettings;
        emit settingsValidityChanged(m_settingsValid);
    }

    const SettingsManager::VlmProvider provider = currentProvider();
    const bool currentValid = !provider.name.trimmed().isEmpty()
        && !provider.baseUrl.trimmed().isEmpty()
        && !provider.model.trimmed().isEmpty();
    m_deleteButton->setEnabled(!m_testing && m_currentIndex >= 0 && m_currentIndex < m_providers.size());
    if (m_saveButton) {
        m_saveButton->setEnabled(!m_testing && !hasInvalid);
    }
    m_testButton->setEnabled(!m_testing && currentValid);
}

SettingsManager::VlmProvider VlmProviderSettingsDialog::defaultProvider() const
{
    SettingsManager::VlmProvider provider;
    provider.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    provider.name = tr("默认 Provider");
    provider.baseUrl = QStringLiteral("https://api.openai.com/v1");
    return provider;
}

SettingsManager::VlmProvider VlmProviderSettingsDialog::currentProvider() const
{
    SettingsManager::VlmProvider provider;
    if (m_currentIndex >= 0 && m_currentIndex < m_providers.size()) {
        provider = m_providers.at(m_currentIndex);
    }
    provider.name = m_nameEdit ? m_nameEdit->text().trimmed() : provider.name;
    provider.baseUrl = m_baseUrlEdit ? m_baseUrlEdit->text().trimmed() : provider.baseUrl;
    provider.model = m_modelEdit ? m_modelEdit->text().trimmed() : provider.model;
    provider.apiKey = m_apiKeyEdit ? m_apiKeyEdit->text() : provider.apiKey;
    return provider;
}

void VlmProviderSettingsDialog::setTestingState(bool testing)
{
    m_testing = testing;
    m_providerList->setEnabled(!testing);
    m_nameEdit->setEnabled(!testing);
    m_baseUrlEdit->setEnabled(!testing);
    m_modelEdit->setEnabled(!testing);
    m_apiKeyEdit->setEnabled(!testing);
    m_addButton->setEnabled(!testing);
    updateButtonState();
}

void VlmProviderSettingsDialog::onProviderSelectionChanged()
{
    if (m_syncing) {
        return;
    }
    populateFields(m_providerList->currentRow());
}

void VlmProviderSettingsDialog::onFieldChanged()
{
    if (m_syncing) {
        return;
    }
    updateSelectedProviderFromFields();
    updateButtonState();
}

void VlmProviderSettingsDialog::addProvider()
{
    updateSelectedProviderFromFields();
    SettingsManager::VlmProvider provider = defaultProvider();
    provider.name = tr("Provider %1").arg(m_providers.size() + 1);
    m_providers.append(provider);
    refreshProviderList();
    m_providerList->setCurrentRow(m_providers.size() - 1);
}

void VlmProviderSettingsDialog::deleteProvider()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_providers.size()) {
        return;
    }
    m_providers.removeAt(m_currentIndex);
    refreshProviderList();
    if (!m_providers.isEmpty()) {
        m_providerList->setCurrentRow(qMin(m_currentIndex, m_providers.size() - 1));
    }
    updateButtonState();
}

void VlmProviderSettingsDialog::testProviderImageInput()
{
    updateSelectedProviderFromFields();
    const SettingsManager::VlmProvider provider = currentProvider();
    if (provider.baseUrl.trimmed().isEmpty() || provider.model.trimmed().isEmpty()) {
        m_testStatusLabel->setText(tr("请先填写 Base URL 和 Model。"));
        return;
    }
    m_testStatusLabel->setText(tr("正在测试图片输入..."));
    setTestingState(true);
    startProviderImageInputTest(VlmAnnotationService::PayloadFormat::OpenAiContentParts);
}

void VlmProviderSettingsDialog::startProviderImageInputTest(VlmAnnotationService::PayloadFormat format)
{
    if (m_testReply) {
        m_testReply->abort();
    }

    const SettingsManager::VlmProvider provider = currentProvider();
    VlmAnnotationService::ApiConfig config;
    config.baseUrl = provider.baseUrl;
    config.model = provider.model;
    config.apiKey = provider.apiKey;

    const QJsonObject payload =
        VlmAnnotationService::buildImageInputTestPayload(config, testImageDataUrl(), format);
    QNetworkReply *reply = m_testNetwork.post(VlmAnnotationService::buildNetworkRequest(config),
                                              QJsonDocument(payload).toJson(QJsonDocument::Compact));
    m_testReply = reply;
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, format]() {
        if (m_testReply == reply) {
            m_testReply = nullptr;
        }
        handleProviderImageInputTestFinished(reply, format);
        reply->deleteLater();
    });
}

void VlmProviderSettingsDialog::handleProviderImageInputTestFinished(
    QNetworkReply *reply,
    VlmAnnotationService::PayloadFormat format)
{
    if (!reply) {
        setTestingState(false);
        return;
    }

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    QString summary;
    if (reply->error() != QNetworkReply::NoError && statusCode == 0) {
        summary = reply->errorString();
    } else {
        summary = responseContentSummary(statusCode, body);
    }

    if (statusCode < 200 || statusCode >= 300) {
        if (format == VlmAnnotationService::PayloadFormat::OpenAiContentParts
            && VlmAnnotationService::shouldRetryWithDashScopePayload(statusCode, summary)) {
            m_testStatusLabel->setText(tr("标准格式被拒绝，正在尝试 DashScope 兼容格式..."));
            startProviderImageInputTest(VlmAnnotationService::PayloadFormat::DashScopeContentParts);
            return;
        }
        m_testStatusLabel->setText(tr("图片输入测试失败：HTTP %1：%2")
                                       .arg(statusCode)
                                       .arg(summary.left(300)));
        setTestingState(false);
        return;
    }

    const QString formatName =
        format == VlmAnnotationService::PayloadFormat::DashScopeContentParts
            ? tr("DashScope 兼容格式")
            : tr("OpenAI 标准格式");
    m_testStatusLabel->setText(tr("支持图片输入（%1）。%2")
                                   .arg(formatName, summary.left(300)));
    setTestingState(false);
}

QString VlmProviderSettingsDialog::testImageDataUrl()
{
    QImage image(32, 32, QImage::Format_RGB32);
    image.fill(Qt::white);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (x < 16 && y < 16) {
                image.setPixelColor(x, y, QColor(230, 40, 40));
            } else if (x >= 16 && y < 16) {
                image.setPixelColor(x, y, QColor(40, 130, 230));
            } else if (x < 16) {
                image.setPixelColor(x, y, QColor(40, 180, 90));
            }
        }
    }

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return QStringLiteral("data:image/png;base64,%1").arg(QString::fromLatin1(bytes.toBase64()));
}

QString VlmProviderSettingsDialog::responseContentSummary(int statusCode, const QByteArray &body)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error == QJsonParseError::NoError && document.isObject()) {
        const QJsonObject root = document.object();
        const QJsonValue errorValue = root.value(QStringLiteral("error"));
        if (errorValue.isObject()) {
            return errorValue.toObject().value(QStringLiteral("message")).toString(
                QString::fromUtf8(body).trimmed());
        }

        const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
        if (!choices.isEmpty()) {
            const QJsonObject message = choices.first()
                                            .toObject()
                                            .value(QStringLiteral("message"))
                                            .toObject();
            const QJsonValue content = message.value(QStringLiteral("content"));
            if (content.isString()) {
                return content.toString().simplified();
            }
        }
    }

    const QString fallback = QString::fromUtf8(body).trimmed();
    if (!fallback.isEmpty()) {
        return fallback;
    }
    return statusCode == 0 ? tr("无 HTTP 响应") : tr("空响应");
}

bool VlmProviderSettingsDialog::saveSettings()
{
    updateSelectedProviderFromFields();
    updateButtonState();
    if (!settingsValid()) {
        return false;
    }
    if (!m_settingsManager) {
        return true;
    }

    m_settingsManager->setVlmProviders(m_providers);
    const int currentRow = m_providerList->currentRow();
    if (currentRow >= 0 && currentRow < m_providers.size()) {
        m_settingsManager->setActiveVlmProviderId(m_providers.at(currentRow).id);
    }
    return true;
}

void VlmProviderSettingsDialog::saveAndAccept()
{
    if (!saveSettings()) {
        return;
    }
    accept();
}

bool VlmProviderSettingsDialog::settingsValid() const
{
    return m_settingsValid;
}
