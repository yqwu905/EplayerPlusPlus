#ifndef VLMANNOTATIONDIALOG_H
#define VLMANNOTATIONDIALOG_H

#include <QDialog>
#include <QHash>
#include <QList>

#include "services/SettingsManager.h"
#include "services/VlmAnnotationService.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTableWidgetItem;
class QTextEdit;
class ImageMarkManager;

class VlmAnnotationDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VlmAnnotationDialog(const QList<VlmAnnotationService::ColumnSnapshot> &columns,
                                 SettingsManager *settingsManager,
                                 ImageMarkManager *markManager,
                                 QWidget *parent = nullptr);
    ~VlmAnnotationDialog() override;

private slots:
    void updateRunButtonState();
    void onTargetColumnChanged();
    void onRunClicked();
    void onCancelClicked();
    void onExportClicked();
    void onManageProvidersClicked();
    void onResultItemDoubleClicked(QTableWidgetItem *item);
    void onServiceProgress(int completed, int total, const QString &statusText);
    void onItemSucceeded(int taskIndex,
                         const QString &id,
                         const QString &category,
                         const QString &reason);
    void onItemFailed(int taskIndex, const QString &id, const QString &error);
    void onServiceFinished(bool cancelled);

private:
    struct ResultRow {
        QString id;
        QString fileName;
        QString category;
        QString reason;
        QString status;
        bool applied = false;
    };

    void setupUi();
    void populateColumns();
    void populateProviders();
    void loadSettings();
    void saveSettings();
    void syncReferenceListForTarget();
    QList<int> selectedReferenceColumns() const;
    int selectedTargetColumn() const;
    SettingsManager::VlmProvider selectedProvider() const;
    VlmAnnotationService::MatchRule selectedMatchRule() const;
    VlmAnnotationService::ApiConfig apiConfig() const;
    void setRunningState(bool running);
    void setupResultsTable(const QList<VlmAnnotationService::Task> &tasks);
    void updateResultRow(int taskIndex, const ResultRow &result);
    QString exportAsCsv() const;
    QString exportAsJson() const;

    QList<VlmAnnotationService::ColumnSnapshot> m_columns;
    SettingsManager *m_settingsManager = nullptr;
    ImageMarkManager *m_markManager = nullptr;
    VlmAnnotationService *m_service = nullptr;

    QComboBox *m_providerCombo = nullptr;
    QPushButton *m_manageProvidersButton = nullptr;
    QTextEdit *m_promptEdit = nullptr;
    QListWidget *m_referenceList = nullptr;
    QComboBox *m_targetCombo = nullptr;
    QComboBox *m_matchRuleCombo = nullptr;
    QSpinBox *m_maxItemsSpin = nullptr;
    QSpinBox *m_concurrencySpin = nullptr;
    QCheckBox *m_skipMarkedCheck = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QLabel *m_statusLabel = nullptr;
    QTableWidget *m_resultTable = nullptr;
    QPushButton *m_runButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPushButton *m_exportButton = nullptr;
    QPushButton *m_closeButton = nullptr;

    QList<VlmAnnotationService::Task> m_tasks;
    QList<ResultRow> m_results;
};

#endif // VLMANNOTATIONDIALOG_H
