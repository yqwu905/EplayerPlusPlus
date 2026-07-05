#ifndef VLMPROVIDERSETTINGSDIALOG_H
#define VLMPROVIDERSETTINGSDIALOG_H

#include <QDialog>
#include <QList>
#include <QNetworkAccessManager>
#include <QPointer>

#include "services/SettingsManager.h"
#include "services/VlmAnnotationService.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QNetworkReply;
class QPushButton;

class VlmProviderSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VlmProviderSettingsDialog(SettingsManager *settingsManager,
                                       QWidget *parent = nullptr,
                                       bool embedded = false);
    ~VlmProviderSettingsDialog() override;

    bool saveSettings();
    bool settingsValid() const;

signals:
    void settingsValidityChanged(bool valid);

private slots:
    void onProviderSelectionChanged();
    void onFieldChanged();
    void addProvider();
    void deleteProvider();
    void testProviderImageInput();
    void saveAndAccept();

private:
    void setupUi();
    void loadProviders();
    void refreshProviderList();
    void populateFields(int index);
    void updateSelectedProviderFromFields();
    void setFieldsEnabled(bool enabled);
    void updateButtonState();
    SettingsManager::VlmProvider defaultProvider() const;
    SettingsManager::VlmProvider currentProvider() const;
    void setTestingState(bool testing);
    void startProviderImageInputTest(VlmAnnotationService::PayloadFormat format);
    void handleProviderImageInputTestFinished(QNetworkReply *reply,
                                              VlmAnnotationService::PayloadFormat format);
    static QString testImageDataUrl();
    static QString responseContentSummary(int statusCode, const QByteArray &body);

    SettingsManager *m_settingsManager = nullptr;
    bool m_embedded = false;
    QList<SettingsManager::VlmProvider> m_providers;
    int m_currentIndex = -1;
    bool m_syncing = false;
    bool m_testing = false;
    bool m_settingsValid = false;

    QListWidget *m_providerList = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_baseUrlEdit = nullptr;
    QLineEdit *m_modelEdit = nullptr;
    QLineEdit *m_apiKeyEdit = nullptr;
    QLabel *m_hintLabel = nullptr;
    QLabel *m_testStatusLabel = nullptr;
    QPushButton *m_addButton = nullptr;
    QPushButton *m_deleteButton = nullptr;
    QPushButton *m_testButton = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_cancelButton = nullptr;

    QNetworkAccessManager m_testNetwork;
    QPointer<QNetworkReply> m_testReply;
};

#endif // VLMPROVIDERSETTINGSDIALOG_H
