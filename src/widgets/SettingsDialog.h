#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>

class QPushButton;
class QTabWidget;
class QCheckBox;
class QWidget;
class SettingsManager;
class VlmProviderSettingsDialog;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Tab {
        Vlm = 0,
        Performance = 1
    };

    explicit SettingsDialog(SettingsManager *settingsManager,
                            QWidget *parent = nullptr);
    ~SettingsDialog() override;

    void setCurrentTab(Tab tab);

private slots:
    void saveAndAccept();
    void updateSaveButtonState();
    void updatePerformanceControls(bool enabled);

private:
    SettingsManager *m_settingsManager = nullptr;
    QTabWidget *m_tabWidget = nullptr;
    VlmProviderSettingsDialog *m_vlmPage = nullptr;
    QWidget *m_performancePage = nullptr;
    QCheckBox *m_windowsNetworkOptimizationsCheck = nullptr;
    QCheckBox *m_windowsNetworkFastEnumerationCheck = nullptr;
    QCheckBox *m_windowsNetworkConstrainedPrefetchCheck = nullptr;
    QCheckBox *m_windowsNetworkDeferredValidationCheck = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
};

#endif // SETTINGSDIALOG_H
