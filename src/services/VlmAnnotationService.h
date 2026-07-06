#ifndef VLMANNOTATIONSERVICE_H
#define VLMANNOTATIONSERVICE_H

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>

class ImageMarkManager;

class VlmAnnotationService : public QObject
{
    Q_OBJECT

public:
    enum class MatchRule {
        Order = 0,
        FileName = 1,
        FileNameFuzzy = 2
    };

    enum class PayloadFormat {
        OpenAiContentParts = 0,
        DashScopeContentParts = 1
    };

    struct ImageItem {
        int columnIndex = -1;
        int row = -1;
        QString folderPath;
        QString columnName;
        QString imagePath;
        QString fileName;
        QString mark;
    };

    struct ColumnSnapshot {
        int columnIndex = -1;
        QString folderPath;
        QString columnName;
        QList<ImageItem> images;
    };

    struct PlanningOptions {
        QList<int> referenceColumns;
        int targetColumn = -1;
        MatchRule matchRule = MatchRule::Order;
        int maxItems = 0;
        bool skipMarked = true;
    };

    struct ReferenceImage {
        ImageItem image;
    };

    struct Task {
        QString id;
        ImageItem target;
        QList<ReferenceImage> references;
    };

    struct ApiConfig {
        QString baseUrl;
        QString model;
        QString apiKey;
        QString userPrompt;
        int maxLongSide = 1536;
        int jpegQuality = 90;
        int batchSize = 4;
        int concurrency = 1;
    };

    struct ParsedResponse {
        bool ok = false;
        QString error;
        QHash<QString, QString> categories;
        QHash<QString, QString> reasons;
    };

    explicit VlmAnnotationService(QObject *parent = nullptr);
    ~VlmAnnotationService() override;

    bool isRunning() const;
    void start(const ApiConfig &config, const QList<Task> &tasks);
    void cancel();

    static QList<Task> buildPlan(const QList<ColumnSnapshot> &columns,
                                 const PlanningOptions &options);
    static ParsedResponse parseAnnotationJson(const QByteArray &content,
                                              const QStringList &expectedIds);
    static ParsedResponse parseHttpResponse(int statusCode,
                                            const QByteArray &body,
                                            const QStringList &expectedIds);
    static ParsedResponse parseStreamingHttpResponse(int statusCode,
                                                     const QByteArray &body,
                                                     const QStringList &expectedIds);
    static QUrl endpointUrl(const QString &baseUrl);
    static QNetworkRequest buildNetworkRequest(const ApiConfig &config);
    static QJsonObject buildChatCompletionsPayload(const ApiConfig &config,
                                                   const QList<Task> &tasks,
                                                   QString *error = nullptr,
                                                   PayloadFormat format = PayloadFormat::OpenAiContentParts);
    static QJsonObject buildImageInputTestPayload(const ApiConfig &config,
                                                  const QString &dataUrl,
                                                  PayloadFormat format = PayloadFormat::OpenAiContentParts);
    static bool shouldRetryWithDashScopePayload(int statusCode, const QString &error);
    static QString imageToDataUrl(const QString &imagePath,
                                  int maxLongSide,
                                  int jpegQuality,
                                  QString *error = nullptr);
    static bool applyAcceptedResult(ImageMarkManager *manager,
                                    const Task &task,
                                    const QString &category,
                                    const QString &reason = QString());
    static int matchRuleToInt(MatchRule rule);
    static MatchRule matchRuleFromInt(int value);

signals:
    void progressChanged(int completed, int total, const QString &statusText);
    void itemSucceeded(int taskIndex,
                       const QString &id,
                       const QString &category,
                       const QString &reason);
    void itemStatusChanged(int taskIndex, const QString &id, const QString &statusText);
    void itemFailed(int taskIndex, const QString &id, const QString &error);
    void finished(bool cancelled);

private:
    void startNextBatch();
    bool sendBatch(int batchStart,
                   const QList<Task> &batch,
                   int retryCount,
                   PayloadFormat format = PayloadFormat::OpenAiContentParts);
    void completeBatch(int batchStart, const QList<Task> &batch);
    static QString builtInFormatPrompt();
    static QString extractJsonObjectText(const QString &content);
    static int findMatchIndex(const ColumnSnapshot &column,
                              const ImageItem &target,
                              MatchRule rule);
    static int levenshteinDistance(const QString &a, const QString &b);

    QNetworkAccessManager m_network;
    QSet<QNetworkReply *> m_currentReplies;
    ApiConfig m_config;
    QList<Task> m_tasks;
    int m_nextIndex = 0;
    int m_completed = 0;
    bool m_running = false;
    bool m_cancelRequested = false;
};

#endif // VLMANNOTATIONSERVICE_H
