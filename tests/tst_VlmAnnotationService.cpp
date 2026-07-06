#include <QtTest>
#include <QDir>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

#include <memory>

#include "services/ImageMarkManager.h"
#include "services/VlmAnnotationService.h"

class tst_VlmAnnotationService : public QObject
{
    Q_OBJECT

private slots:
    void buildPlan_orderUsesCurrentRowsLimitAndSkipMarked();
    void buildPlan_fileNameExactAndFuzzy();
    void parseAnnotationJson_acceptsIdMappingsAndMarkdown();
    void parseAnnotationJson_rejectsSchemaProblems();
    void parseHttpResponse_handlesOpenAiAndHttpErrors();
    void parseStreamingHttpResponse_handlesSseDeltasAndFallback();
    void start_parsesStreamingNetworkResponse();
    void requestHelpers_buildOpenAiCompatiblePayload();
    void applyAcceptedResult_writesOnlyTarget();
};

namespace
{
using Service = VlmAnnotationService;

Service::ImageItem item(int column,
                        int row,
                        const QString &folder,
                        const QString &fileName,
                        const QString &mark = QString())
{
    Service::ImageItem image;
    image.columnIndex = column;
    image.row = row;
    image.folderPath = folder;
    image.columnName = QStringLiteral("col%1").arg(column);
    image.fileName = fileName;
    image.imagePath = QDir(folder).filePath(fileName);
    image.mark = mark;
    return image;
}

Service::ColumnSnapshot column(int columnIndex,
                               const QString &folder,
                               const QStringList &fileNames,
                               const QStringList &marks = {})
{
    Service::ColumnSnapshot snapshot;
    snapshot.columnIndex = columnIndex;
    snapshot.folderPath = folder;
    snapshot.columnName = QStringLiteral("col%1").arg(columnIndex);
    for (int i = 0; i < fileNames.size(); ++i) {
        snapshot.images.append(item(columnIndex,
                                    i,
                                    folder,
                                    fileNames.at(i),
                                    marks.value(i)));
    }
    return snapshot;
}

QString createImage(QTemporaryDir &dir, const QString &name, const QColor &color)
{
    QImage image(18, 12, QImage::Format_ARGB32);
    image.fill(color);
    const QString path = dir.filePath(name);
    if (!image.save(path)) {
        return QString();
    }
    return path;
}

QByteArray streamChunk(const QString &content)
{
    QJsonObject delta;
    delta.insert(QStringLiteral("content"), content);
    QJsonObject choice;
    choice.insert(QStringLiteral("delta"), delta);
    QJsonObject root;
    root.insert(QStringLiteral("choices"), QJsonArray{choice});
    return "data: " + QJsonDocument(root).toJson(QJsonDocument::Compact) + "\n\n";
}

int requestContentLength(const QByteArray &request)
{
    const QList<QByteArray> lines = request.left(request.indexOf("\r\n\r\n")).split('\n');
    for (QByteArray line : lines) {
        line = line.trimmed();
        if (!line.toLower().startsWith("content-length:")) {
            continue;
        }
        return line.mid(line.indexOf(':') + 1).trimmed().toInt();
    }
    return 0;
}

void writeHttpChunk(QTcpSocket *socket, const QByteArray &chunk)
{
    socket->write(QByteArray::number(chunk.size(), 16));
    socket->write("\r\n");
    socket->write(chunk);
    socket->write("\r\n");
}

void writeStreamingResponse(QTcpSocket *socket, const QString &content)
{
    socket->write("HTTP/1.1 200 OK\r\n"
                  "Content-Type: text/event-stream; charset=utf-8\r\n"
                  "Transfer-Encoding: chunked\r\n"
                  "Connection: close\r\n"
                  "\r\n");
    writeHttpChunk(socket, streamChunk(content.left(20)));
    writeHttpChunk(socket, streamChunk(content.mid(20)));
    writeHttpChunk(socket, "data: [DONE]\n\n");
    socket->write("0\r\n\r\n");
    socket->disconnectFromHost();
}
}

void tst_VlmAnnotationService::buildPlan_orderUsesCurrentRowsLimitAndSkipMarked()
{
    QTemporaryDir refDir;
    QTemporaryDir targetDir;
    QVERIFY(refDir.isValid());
    QVERIFY(targetDir.isValid());

    QList<Service::ColumnSnapshot> columns = {
        column(0, refDir.path(), {"r0.png", "r1.png", "r2.png"}),
        column(1, targetDir.path(), {"t0.png", "t1.png", "t2.png"}, {"A", "", ""})
    };

    Service::PlanningOptions options;
    options.referenceColumns = {0};
    options.targetColumn = 1;
    options.matchRule = Service::MatchRule::Order;
    options.maxItems = 1;
    options.skipMarked = true;

    const QList<Service::Task> tasks = Service::buildPlan(columns, options);
    QCOMPARE(tasks.size(), 1);
    QCOMPARE(tasks.first().id, QStringLiteral("T0001"));
    QCOMPARE(tasks.first().target.fileName, QStringLiteral("t1.png"));
    QCOMPARE(tasks.first().references.size(), 1);
    QCOMPARE(tasks.first().references.first().image.fileName, QStringLiteral("r1.png"));
}

void tst_VlmAnnotationService::buildPlan_fileNameExactAndFuzzy()
{
    QTemporaryDir refDir;
    QTemporaryDir targetDir;
    QVERIFY(refDir.isValid());
    QVERIFY(targetDir.isValid());

    QList<Service::ColumnSnapshot> columns = {
        column(0, refDir.path(), {"dogs.png", "cats.png"}),
        column(1, targetDir.path(), {"cat.png"})
    };

    Service::PlanningOptions options;
    options.referenceColumns = {0};
    options.targetColumn = 1;
    options.matchRule = Service::MatchRule::FileName;
    options.skipMarked = false;

    QList<Service::Task> tasks = Service::buildPlan(columns, options);
    QCOMPARE(tasks.size(), 1);
    QCOMPARE(tasks.first().references.size(), 0);

    options.matchRule = Service::MatchRule::FileNameFuzzy;
    tasks = Service::buildPlan(columns, options);
    QCOMPARE(tasks.size(), 1);
    QCOMPARE(tasks.first().references.size(), 1);
    QCOMPARE(tasks.first().references.first().image.fileName, QStringLiteral("cats.png"));
}

void tst_VlmAnnotationService::parseAnnotationJson_acceptsIdMappingsAndMarkdown()
{
    const QByteArray content =
        "```json\n{\"result\":{\"T0001\":\"a\",\"T0002\":\"F\"},"
        "\"reason\":{\"T0001\":\"first\",\"T0002\":\"second\"}}\n```";

    const Service::ParsedResponse parsed =
        Service::parseAnnotationJson(content, {"T0001", "T0002"});
    QVERIFY2(parsed.ok, qPrintable(parsed.error));
    QCOMPARE(parsed.categories.value(QStringLiteral("T0001")), QStringLiteral("A"));
    QCOMPARE(parsed.categories.value(QStringLiteral("T0002")), QStringLiteral("F"));
    QCOMPARE(parsed.reasons.value(QStringLiteral("T0002")), QStringLiteral("second"));
}

void tst_VlmAnnotationService::parseAnnotationJson_rejectsSchemaProblems()
{
    QVERIFY(!Service::parseAnnotationJson(
        "{\"result\":{\"T0001\":\"G\"},\"reason\":{\"T0001\":\"bad\"}}",
        {"T0001"}).ok);

    QVERIFY(!Service::parseAnnotationJson(
        "{\"result\":{\"T0001\":\"A\"},\"reason\":{}}",
        {"T0001"}).ok);

    QVERIFY(!Service::parseAnnotationJson(
        "{\"result\":{\"T0001\":\"A\",\"T9999\":\"B\"},\"reason\":{\"T0001\":\"ok\"}}",
        {"T0001"}).ok);

    QVERIFY(!Service::parseAnnotationJson(
        "{\"result\":[\"A\"],\"reason\":{\"T0001\":\"ok\"}}",
        {"T0001"}).ok);
}

void tst_VlmAnnotationService::parseHttpResponse_handlesOpenAiAndHttpErrors()
{
    QJsonObject content;
    content.insert(QStringLiteral("result"), QJsonObject{{QStringLiteral("T0001"), QStringLiteral("C")}});
    content.insert(QStringLiteral("reason"), QJsonObject{{QStringLiteral("T0001"), QStringLiteral("clear reason")}});

    QJsonObject message;
    message.insert(QStringLiteral("content"),
                   QString::fromUtf8(QJsonDocument(content).toJson(QJsonDocument::Compact)));
    QJsonObject choice;
    choice.insert(QStringLiteral("message"), message);
    QJsonObject response;
    response.insert(QStringLiteral("choices"), QJsonArray{choice});

    const Service::ParsedResponse parsed =
        Service::parseHttpResponse(200,
                                   QJsonDocument(response).toJson(QJsonDocument::Compact),
                                   {"T0001"});
    QVERIFY2(parsed.ok, qPrintable(parsed.error));
    QCOMPARE(parsed.categories.value(QStringLiteral("T0001")), QStringLiteral("C"));

    const Service::ParsedResponse failed =
        Service::parseHttpResponse(500, QByteArrayLiteral("{\"error\":\"boom\"}"), {"T0001"});
    QVERIFY(!failed.ok);
    QVERIFY(failed.error.contains(QStringLiteral("HTTP 500")));
}

void tst_VlmAnnotationService::parseStreamingHttpResponse_handlesSseDeltasAndFallback()
{
    const QString content =
        QStringLiteral("{\"result\":{\"T0001\":\"B\"},\"reason\":{\"T0001\":\"stream ok\"}}");
    QByteArray body;
    body += "event: message\n";
    body += streamChunk(content.left(18));
    body += streamChunk(content.mid(18));
    body += "data: [DONE]\n\n";

    const Service::ParsedResponse streamed =
        Service::parseStreamingHttpResponse(200, body, {"T0001"});
    QVERIFY2(streamed.ok, qPrintable(streamed.error));
    QCOMPARE(streamed.categories.value(QStringLiteral("T0001")), QStringLiteral("B"));
    QCOMPARE(streamed.reasons.value(QStringLiteral("T0001")), QStringLiteral("stream ok"));

    QJsonObject message;
    message.insert(QStringLiteral("content"),
                   QStringLiteral("{\"result\":{\"T0001\":\"E\"},\"reason\":{\"T0001\":\"full json\"}}"));
    QJsonObject choice;
    choice.insert(QStringLiteral("message"), message);
    QJsonObject response;
    response.insert(QStringLiteral("choices"), QJsonArray{choice});

    const Service::ParsedResponse fallback =
        Service::parseStreamingHttpResponse(200,
                                            QJsonDocument(response).toJson(QJsonDocument::Compact),
                                            {"T0001"});
    QVERIFY2(fallback.ok, qPrintable(fallback.error));
    QCOMPARE(fallback.categories.value(QStringLiteral("T0001")), QStringLiteral("E"));
}

void tst_VlmAnnotationService::start_parsesStreamingNetworkResponse()
{
    QTemporaryDir targetDir;
    QVERIFY(targetDir.isValid());
    QVERIFY(!createImage(targetDir, QStringLiteral("target.png"), Qt::red).isEmpty());

    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0)) {
        QSKIP(qPrintable(QStringLiteral("Local TCP listen unavailable: %1")
                             .arg(server.errorString())),
              SkipSingle);
    }

    const QString streamedContent =
        QStringLiteral("{\"result\":{\"T0001\":\"D\"},\"reason\":{\"T0001\":\"network stream\"}}");
    connect(&server, &QTcpServer::newConnection, this, [&server, streamedContent]() {
        QTcpSocket *socket = server.nextPendingConnection();
        QVERIFY(socket);

        auto request = std::make_shared<QByteArray>();
        auto responded = std::make_shared<bool>(false);
        connect(socket, &QTcpSocket::readyRead, socket, [socket, request, responded, streamedContent]() {
            request->append(socket->readAll());
            if (*responded) {
                return;
            }

            const int headerEnd = request->indexOf("\r\n\r\n");
            if (headerEnd < 0) {
                return;
            }
            const int contentLength = requestContentLength(*request);
            if (request->size() < headerEnd + 4 + contentLength) {
                return;
            }

            *responded = true;
            writeStreamingResponse(socket, streamedContent);
        });
    });

    Service::Task task;
    task.id = QStringLiteral("T0001");
    task.target = item(0, 0, targetDir.path(), QStringLiteral("target.png"));

    Service::ApiConfig config;
    config.baseUrl = QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort());
    config.model = QStringLiteral("streaming-model");
    config.userPrompt = QStringLiteral("Classify.");

    Service service;
    QSignalSpy succeededSpy(&service, &Service::itemSucceeded);
    QSignalSpy failedSpy(&service, &Service::itemFailed);
    QSignalSpy finishedSpy(&service, &Service::finished);
    QSignalSpy statusSpy(&service, &Service::itemStatusChanged);

    service.start(config, {task});

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(succeededSpy.count(), 1);
    QCOMPARE(succeededSpy.first().at(0).toInt(), 0);
    QCOMPARE(succeededSpy.first().at(1).toString(), QStringLiteral("T0001"));
    QCOMPARE(succeededSpy.first().at(2).toString(), QStringLiteral("D"));
    QCOMPARE(succeededSpy.first().at(3).toString(), QStringLiteral("network stream"));

    bool sawStreamingStatus = false;
    for (const auto &call : statusSpy) {
        if (call.at(2).toString().contains(QStringLiteral("流式响应"))) {
            sawStreamingStatus = true;
            break;
        }
    }
    QVERIFY(sawStreamingStatus);
}

void tst_VlmAnnotationService::requestHelpers_buildOpenAiCompatiblePayload()
{
    QTemporaryDir targetDir;
    QTemporaryDir refDir;
    QVERIFY(targetDir.isValid());
    QVERIFY(refDir.isValid());
    QVERIFY(!createImage(targetDir, QStringLiteral("target.png"), Qt::red).isEmpty());
    QVERIFY(!createImage(refDir, QStringLiteral("ref.png"), Qt::green).isEmpty());

    Service::Task task;
    task.id = QStringLiteral("T0001");
    task.target = item(1, 0, targetDir.path(), QStringLiteral("target.png"));
    task.references.append({item(0, 0, refDir.path(), QStringLiteral("ref.png"))});

    Service::ApiConfig config;
    config.baseUrl = QStringLiteral("https://example.test/v1/");
    config.model = QStringLiteral("vision-model");
    config.apiKey = QStringLiteral("secret");
    config.userPrompt = QStringLiteral("A means best.");

    QCOMPARE(Service::endpointUrl(config.baseUrl).toString(),
             QStringLiteral("https://example.test/v1/chat/completions"));
    const QNetworkRequest request = Service::buildNetworkRequest(config);
    QCOMPARE(QString::fromUtf8(request.rawHeader("Authorization")),
             QStringLiteral("Bearer secret"));

    QString error;
    const QJsonObject payload = Service::buildChatCompletionsPayload(config, {task}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(payload.value(QStringLiteral("model")).toString(), QStringLiteral("vision-model"));
    QCOMPARE(payload.value(QStringLiteral("response_format")).toObject().value(QStringLiteral("type")).toString(),
             QStringLiteral("json_object"));

    const QJsonArray messages = payload.value(QStringLiteral("messages")).toArray();
    QCOMPARE(messages.size(), 1);
    const QJsonArray userContent = messages.at(0).toObject().value(QStringLiteral("content")).toArray();
    QVERIFY(userContent.size() >= 5);
    bool sawImageUrl = false;
    for (const QJsonValue &entry : userContent) {
        const QJsonObject object = entry.toObject();
        if (object.value(QStringLiteral("type")).toString() == QStringLiteral("image_url")) {
            sawImageUrl = object.value(QStringLiteral("image_url"))
                              .toObject()
                              .value(QStringLiteral("url"))
                              .toString()
                              .startsWith(QStringLiteral("data:image/"));
        }
    }
    QVERIFY(sawImageUrl);

    const QJsonObject dashScopePayload =
        Service::buildChatCompletionsPayload(config,
                                             {task},
                                             &error,
                                             Service::PayloadFormat::DashScopeContentParts);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const QJsonArray dashScopeContent = dashScopePayload.value(QStringLiteral("messages"))
                                           .toArray()
                                           .at(0)
                                           .toObject()
                                           .value(QStringLiteral("content"))
                                           .toArray();
    bool sawDashScopeImage = false;
    for (const QJsonValue &entry : dashScopeContent) {
        const QJsonObject object = entry.toObject();
        if (object.value(QStringLiteral("image")).toString().startsWith(QStringLiteral("data:image/"))) {
            sawDashScopeImage = true;
        }
        QVERIFY(!object.contains(QStringLiteral("type")) ||
                object.value(QStringLiteral("type")).toString() == QStringLiteral("text") ||
                object.value(QStringLiteral("type")).toString() == QStringLiteral("image_url"));
    }
    QVERIFY(sawDashScopeImage);

    const QJsonObject testPayload = Service::buildImageInputTestPayload(
        config,
        QStringLiteral("data:image/png;base64,abc"),
        Service::PayloadFormat::DashScopeContentParts);
    const QJsonArray testContent = testPayload.value(QStringLiteral("messages"))
                                        .toArray()
                                        .at(0)
                                        .toObject()
                                        .value(QStringLiteral("content"))
                                        .toArray();
    QCOMPARE(testContent.at(1).toObject().value(QStringLiteral("image")).toString(),
             QStringLiteral("data:image/png;base64,abc"));

    QVERIFY(Service::shouldRetryWithDashScopePayload(
        400,
        QStringLiteral("InternalError.Algo.InvalidParameter: Unexpected item type in content.")));
}

void tst_VlmAnnotationService::applyAcceptedResult_writesOnlyTarget()
{
    QTemporaryDir targetDir;
    QTemporaryDir refDir;
    QVERIFY(targetDir.isValid());
    QVERIFY(refDir.isValid());
    QVERIFY(!createImage(targetDir, QStringLiteral("target.png"), Qt::red).isEmpty());
    QVERIFY(!createImage(refDir, QStringLiteral("ref.png"), Qt::green).isEmpty());

    Service::Task task;
    task.id = QStringLiteral("T0001");
    task.target = item(1, 0, targetDir.path(), QStringLiteral("target.png"));
    task.references.append({item(0, 0, refDir.path(), QStringLiteral("ref.png"))});

    ImageMarkManager manager;
    const QString reason = QStringLiteral("The model selected D because the target is darker.");
    QVERIFY(Service::applyAcceptedResult(&manager, task, QStringLiteral("D"), reason));
    QCOMPARE(manager.markForImage(targetDir.path(), targetDir.filePath("target.png")),
             QStringLiteral("D"));
    const ImageMarkManager::MarkMetadata targetMetadata =
        manager.markMetadataForImage(targetDir.path(), targetDir.filePath("target.png"));
    QCOMPARE(targetMetadata.source, ImageMarkManager::vlmSource());
    QCOMPARE(targetMetadata.reason, reason);
    QVERIFY(manager.markForImage(refDir.path(), refDir.filePath("ref.png")).isEmpty());
    QVERIFY(manager.markMetadataForImage(refDir.path(), refDir.filePath("ref.png")).reason.isEmpty());
}

QTEST_MAIN(tst_VlmAnnotationService)
#include "tst_VlmAnnotationService.moc"
