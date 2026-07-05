#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>

class QPushButton;
class QTabWidget;
class SettingsManager;
class VlmProviderSettingsDialog;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Tab {
        Vlm = 0
    };

    explicit SettingsDialog(SettingsManager *settingsManager,
                            QWidget *parent = nullptr);
    ~SettingsDialog() override;

    void setCurrentTab(Tab tab);

private slots:
    void saveAndAccept();

private:
    QTabWidget *m_tabWidget = nullptr;
    VlmProviderSettingsDialog *m_vlmPage = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
};

#endif // SETTINGSDIALOG_H
