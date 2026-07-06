#include "VlmAnnotationService.h"
#include "ImageMarkManager.h"

#include <QBuffer>
#include <QByteArray>
#include <QCoreApplication>
#include <QEventLoop>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QSet>
#include <QVector>

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>

namespace
{
QJsonObject textContent(const QString &text, VlmAnnotationService::PayloadFormat format)
{
    QJsonObject object;
    if (format == VlmAnnotationService::PayloadFormat::DashScopeContentParts) {
        object.insert(QStringLiteral("text"), text);
    } else {
        object.insert(QStringLiteral("type"), QStringLiteral("text"));
        object.insert(QStringLiteral("text"), text);
    }
    return object;
}

QJsonObject imageContent(const QString &dataUrl, VlmAnnotationService::PayloadFormat format)
{
    QJsonObject object;
    if (format == VlmAnnotationService::PayloadFormat::DashScopeContentParts) {
        object.insert(QStringLiteral("image"), dataUrl);
    } else {
        QJsonObject imageUrl;
        imageUrl.insert(QStringLiteral("url"), dataUrl);
        object.insert(QStringLiteral("type"), QStringLiteral("image_url"));
        object.insert(QStringLiteral("image_url"), imageUrl);
    }
    return object;
}

QJsonObject messageObject(const QString &role, const QJsonValue &content)
{
    QJsonObject object;
    object.insert(QStringLiteral("role"), role);
    object.insert(QStringLiteral("content"), content);
    return object;
}

QString payloadFormatName(VlmAnnotationService::PayloadFormat format)
{
    return format == VlmAnnotationService::PayloadFormat::DashScopeContentParts
        ? QObject::tr("DashScope 兼容格式")
        : QObject::tr("OpenAI 标准格式");
}

QString byteCountText(qint64 bytes)
{
    if (bytes < 0) {
        return QObject::tr("未知大小");
    }
    if (bytes >= 1024LL * 1024LL) {
        return QStringLiteral("%1 MB").arg(bytes / 1024.0 / 1024.0, 0, 'f', 1);
    }
    if (bytes >= 1024LL) {
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 B").arg(bytes);
}

int imageCountForBatch(const QList<VlmAnnotationService::Task> &batch)
{
    int imageCount = 0;
    for (const auto &task : batch) {
        imageCount += 1 + task.references.size();
    }
    return imageCount;
}

void flushStatusEvents()
{
    if (QCoreApplication::instance()) {
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
}

struct StreamingReplyState {
    QByteArray rawBody;
    QByteArray pendingLine;
    QString content;
    QString error;
    bool sawStreamData = false;
    bool sawDone = false;
};

QString contentFromChoice(const QJsonObject &choice)
{
    const QJsonObject delta = choice.value(QStringLiteral("delta")).toObject();
    const QJsonValue deltaContent = delta.value(QStringLiteral("content"));
    if (deltaContent.isString()) {
        return deltaContent.toString();
    }

    const QJsonObject message = choice.value(QStringLiteral("message")).toObject();
    const QJsonValue messageContent = message.value(QStringLiteral("content"));
    if (messageContent.isString()) {
        return messageContent.toString();
    }

    const QJsonValue text = choice.value(QStringLiteral("text"));
    return text.isString() ? text.toString() : QString();
}

bool appendStreamingJsonPayload(StreamingReplyState *state, const QByteArray &payload)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (state && state->error.isEmpty()) {
            state->error = QObject::tr("流式 JSON 解析失败：%1").arg(parseError.errorString());
        }
        return false;
    }

    if (!state) {
        return false;
    }

    const QJsonObject root = document.object();
    const QJsonValue errorValue = root.value(QStringLiteral("error"));
    if (errorValue.isObject() && state->error.isEmpty()) {
        state->error = errorValue.toObject().value(QStringLiteral("message")).toString(
            QString::fromUtf8(payload).trimmed());
    } else if (errorValue.isString() && state->error.isEmpty()) {
        state->error = errorValue.toString();
    }

    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (!choices.isEmpty()) {
        state->sawStreamData = true;
        for (const QJsonValue &choiceValue : choices) {
            state->content += contentFromChoice(choiceValue.toObject());
        }
        return true;
    }

    if (root.value(QStringLiteral("result")).isObject() &&
        root.value(QStringLiteral("reason")).isObject()) {
        state->sawStreamData = true;
        state->content += QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
        return true;
    }

    return false;
}

void processStreamingLine(StreamingReplyState *state, QByteArray line)
{
    if (!state) {
        return;
    }
    if (line.endsWith('\r')) {
        line.chop(1);
    }

    const QByteArray trimmed = line.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith(':')) {
        return;
    }

    QByteArray payload;
    if (trimmed.startsWith("data:")) {
        payload = trimmed.mid(5).trimmed();
    } else if (trimmed.startsWith('{')) {
        payload = trimmed;
    } else {
        return;
    }

    if (payload == "[DONE]") {
        state->sawStreamData = true;
        state->sawDone = true;
        return;
    }

    appendStreamingJsonPayload(state, payload);
}

void processStreamingChunk(StreamingReplyState *state, const QByteArray &chunk)
{
    if (!state || chunk.isEmpty()) {
        return;
    }

    state->rawBody += chunk;
    state->pendingLine += chunk;
    while (true) {
        const int newline = state->pendingLine.indexOf('\n');
        if (newline < 0) {
            break;
        }
        const QByteArray line = state->pendingLine.left(newline);
        state->pendingLine.remove(0, newline + 1);
        processStreamingLine(state, line);
    }
}

void finalizeStreamingState(StreamingReplyState *state)
{
    if (!state || state->pendingLine.isEmpty()) {
        return;
    }
    const QByteArray line = state->pendingLine;
    state->pendingLine.clear();
    processStreamingLine(state, line);
}

}

VlmAnnotationService::VlmAnnotationService(QObject *parent)
    : QObject(parent)
{
}

VlmAnnotationService::~VlmAnnotationService()
{
    cancel();
}

bool VlmAnnotationService::isRunning() const
{
    return m_running;
}

void VlmAnnotationService::start(const ApiConfig &config, const QList<Task> &tasks)
{
    if (m_running) {
        return;
    }

    m_config = config;
    m_config.batchSize = qBound(1, m_config.batchSize, 16);
    m_config.concurrency = qBound(1, m_config.concurrency, 16);
    m_tasks = tasks;
    m_nextIndex = 0;
    m_completed = 0;
    m_currentReplies.clear();
    m_cancelRequested = false;
    m_running = true;

    emit progressChanged(0, m_tasks.size(), tr("准备 AI 标注"));

    if (m_tasks.isEmpty()) {
        m_running = false;
        emit finished(false);
        return;
    }

    startNextBatch();
}

void VlmAnnotationService::cancel()
{
    if (!m_running && m_currentReplies.isEmpty()) {
        return;
    }

    m_cancelRequested = true;
    const QSet<QNetworkReply *> replies = m_currentReplies;
    for (QNetworkReply *reply : replies) {
        if (reply) {
            reply->abort();
        }
    }
    startNextBatch();
}

QList<VlmAnnotationService::Task> VlmAnnotationService::buildPlan(
    const QList<ColumnSnapshot> &columns,
    const PlanningOptions &options)
{
    const auto targetIt = std::find_if(columns.cbegin(), columns.cend(),
                                       [&options](const ColumnSnapshot &column) {
        return column.columnIndex == options.targetColumn;
    });
    if (targetIt == columns.cend()) {
        return {};
    }

    QSet<int> referenceSet;
    for (int columnIndex : options.referenceColumns) {
        if (columnIndex != options.targetColumn) {
            referenceSet.insert(columnIndex);
        }
    }

    QList<ColumnSnapshot> references;
    for (const ColumnSnapshot &column : columns) {
        if (referenceSet.contains(column.columnIndex)) {
            references.append(column);
        }
    }

    QList<Task> tasks;
    const ColumnSnapshot &targetColumn = *targetIt;
    for (int row = 0; row < targetColumn.images.size(); ++row) {
        const ImageItem &target = targetColumn.images.at(row);
        if (options.skipMarked && !target.mark.trimmed().isEmpty()) {
            continue;
        }

        Task task;
        task.id = QStringLiteral("T%1").arg(tasks.size() + 1, 4, 10, QLatin1Char('0'));
        task.target = target;

        for (const ColumnSnapshot &referenceColumn : std::as_const(references)) {
            const int matchIndex = findMatchIndex(referenceColumn, target, options.matchRule);
            if (matchIndex < 0 || matchIndex >= referenceColumn.images.size()) {
                continue;
            }
            task.references.append({referenceColumn.images.at(matchIndex)});
        }

        tasks.append(task);
        if (options.maxItems > 0 && tasks.size() >= options.maxItems) {
            break;
        }
    }
    return tasks;
}

VlmAnnotationService::ParsedResponse VlmAnnotationService::parseAnnotationJson(
    const QByteArray &content,
    const QStringList &expectedIds)
{
    ParsedResponse parsed;
    const QString jsonText = extractJsonObjectText(QString::fromUtf8(content));
    if (jsonText.isEmpty()) {
        parsed.error = QObject::tr("未找到 JSON 对象");
        return parsed;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(jsonText.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        parsed.error = QObject::tr("JSON 解析失败：%1").arg(parseError.errorString());
        return parsed;
    }

    const QJsonObject root = document.object();
    const QJsonValue resultValue = root.value(QStringLiteral("result"));
    const QJsonValue reasonValue = root.value(QStringLiteral("reason"));
    if (!resultValue.isObject() || !reasonValue.isObject()) {
        parsed.error = QObject::tr("JSON 必须包含对象类型的 result 和 reason");
        return parsed;
    }

    const QJsonObject result = resultValue.toObject();
    const QJsonObject reason = reasonValue.toObject();
    QSet<QString> expectedSet;
    for (const QString &id : expectedIds) {
        expectedSet.insert(id);
    }

    for (auto it = result.constBegin(); it != result.constEnd(); ++it) {
        if (!expectedSet.contains(it.key())) {
            parsed.error = QObject::tr("result 包含未知 ID：%1").arg(it.key());
            return parsed;
        }
    }
    for (auto it = reason.constBegin(); it != reason.constEnd(); ++it) {
        if (!expectedSet.contains(it.key())) {
            parsed.error = QObject::tr("reason 包含未知 ID：%1").arg(it.key());
            return parsed;
        }
    }

    for (const QString &id : expectedIds) {
        if (!result.contains(id)) {
            parsed.error = QObject::tr("result 缺少 ID：%1").arg(id);
            return parsed;
        }
        if (!reason.contains(id)) {
            parsed.error = QObject::tr("reason 缺少 ID：%1").arg(id);
            return parsed;
        }

        const QString category = result.value(id).toString().trimmed().toUpper();
        if (!ImageMarkManager::isValidCategory(category)) {
            parsed.error = QObject::tr("%1 的分类无效：%2").arg(id, category);
            return parsed;
        }
        if (!reason.value(id).isString()) {
            parsed.error = QObject::tr("%1 的 reason 必须是字符串").arg(id);
            return parsed;
        }

        parsed.categories.insert(id, category);
        parsed.reasons.insert(id, reason.value(id).toString().trimmed());
    }

    parsed.ok = true;
    return parsed;
}

VlmAnnotationService::ParsedResponse VlmAnnotationService::parseHttpResponse(
    int statusCode,
    const QByteArray &body,
    const QStringList &expectedIds)
{
    ParsedResponse parsed;
    if (statusCode < 200 || statusCode >= 300) {
        const QString summary = QString::fromUtf8(body).trimmed().left(500);
        parsed.error = QObject::tr("HTTP %1：%2").arg(statusCode).arg(summary);
        return parsed;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error == QJsonParseError::NoError && document.isObject()) {
        const QJsonObject root = document.object();
        const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
        if (!choices.isEmpty()) {
            const QJsonObject message = choices.first()
                                            .toObject()
                                            .value(QStringLiteral("message"))
                                            .toObject();
            const QJsonValue content = message.value(QStringLiteral("content"));
            if (content.isString()) {
                return parseAnnotationJson(content.toString().toUtf8(), expectedIds);
            }
            parsed.error = QObject::tr("OpenAI 响应缺少 message.content 字符串");
            return parsed;
        }
    }

    return parseAnnotationJson(body, expectedIds);
}

VlmAnnotationService::ParsedResponse VlmAnnotationService::parseStreamingHttpResponse(
    int statusCode,
    const QByteArray &body,
    const QStringList &expectedIds)
{
    ParsedResponse parsed;
    if (statusCode < 200 || statusCode >= 300) {
        const QString summary = QString::fromUtf8(body).trimmed().left(500);
        parsed.error = QObject::tr("HTTP %1：%2").arg(statusCode).arg(summary);
        return parsed;
    }

    StreamingReplyState state;
    processStreamingChunk(&state, body);
    finalizeStreamingState(&state);

    if (!state.sawStreamData) {
        return parseHttpResponse(statusCode, body, expectedIds);
    }

    if (state.content.trimmed().isEmpty()) {
        parsed.error = state.error.isEmpty()
            ? QObject::tr("流式响应没有输出 message.content")
            : state.error;
        return parsed;
    }

    parsed = parseAnnotationJson(state.content.toUtf8(), expectedIds);
    if (!parsed.ok && !state.error.isEmpty()) {
        parsed.error = QObject::tr("%1；流式错误：%2").arg(parsed.error, state.error);
    }
    return parsed;
}

QUrl VlmAnnotationService::endpointUrl(const QString &baseUrl)
{
    QString normalized = baseUrl.trimmed();
    if (normalized.isEmpty()) {
        normalized = QStringLiteral("https://api.openai.com/v1");
    }
    while (normalized.endsWith(QLatin1Char('/'))) {
        normalized.chop(1);
    }
    if (normalized.endsWith(QStringLiteral("/chat/completions"))) {
        return QUrl(normalized);
    }
    return QUrl(normalized + QStringLiteral("/chat/completions"));
}

QNetworkRequest VlmAnnotationService::buildNetworkRequest(const ApiConfig &config)
{
    QNetworkRequest request(endpointUrl(config.baseUrl));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!config.apiKey.trimmed().isEmpty()) {
        request.setRawHeader("Authorization",
                             QStringLiteral("Bearer %1")
                                 .arg(config.apiKey.trimmed())
                                 .toUtf8());
    }
    return request;
}

QJsonObject VlmAnnotationService::buildChatCompletionsPayload(const ApiConfig &config,
                                                              const QList<Task> &tasks,
                                                              QString *error,
                                                              PayloadFormat format)
{
    if (error) {
        error->clear();
    }

    QJsonArray content;
    QStringList targetLines;
    targetLines << builtInFormatPrompt()
                << QString()
                << QObject::tr("标注要求：")
                << config.userPrompt.trimmed()
                << QString()
                << QObject::tr("本批目标图片。只为 TARGET 图片分类，REFERENCE 图片只作为上下文：");
    for (const Task &task : tasks) {
        targetLines << QObject::tr("- %1: 列=%2, 文件=%3, 匹配参考数=%4")
                           .arg(task.id,
                                task.target.columnName,
                                task.target.fileName)
                           .arg(task.references.size());
    }
    content.append(textContent(targetLines.join(QLatin1Char('\n')), format));

    for (const Task &task : tasks) {
        content.append(textContent(QObject::tr("TARGET %1 / %2 / %3")
                                       .arg(task.id,
                                            task.target.columnName,
                                            task.target.fileName),
                                   format));
        const QString targetData = imageToDataUrl(task.target.imagePath,
                                                  config.maxLongSide,
                                                  config.jpegQuality,
                                                  error);
        if (targetData.isEmpty()) {
            return {};
        }
        content.append(imageContent(targetData, format));

        for (const ReferenceImage &reference : task.references) {
            content.append(textContent(QObject::tr("REFERENCE for %1 / %2 / %3")
                                           .arg(task.id,
                                                reference.image.columnName,
                                                reference.image.fileName),
                                       format));
            const QString refData = imageToDataUrl(reference.image.imagePath,
                                                   config.maxLongSide,
                                                   config.jpegQuality,
                                                   error);
            if (refData.isEmpty()) {
                return {};
            }
            content.append(imageContent(refData, format));
        }
    }

    QJsonArray messages;
    messages.append(messageObject(QStringLiteral("user"), content));

    QJsonObject responseFormat;
    responseFormat.insert(QStringLiteral("type"), QStringLiteral("json_object"));

    QJsonObject root;
    root.insert(QStringLiteral("model"), config.model.trimmed());
    root.insert(QStringLiteral("messages"), messages);
    root.insert(QStringLiteral("response_format"), responseFormat);
    return root;
}

QJsonObject VlmAnnotationService::buildImageInputTestPayload(const ApiConfig &config,
                                                             const QString &dataUrl,
                                                             PayloadFormat format)
{
    QJsonArray content;
    content.append(textContent(QObject::tr(
        "这是一次 VLM 图片输入能力测试。请观察随后的测试图片，并只返回 JSON："
        "{\"image_supported\":true,\"summary\":\"用一句话说明你看到了什么\"}。"
        "如果你无法读取图片，则返回 {\"image_supported\":false,\"summary\":\"无法读取图片\"}。"),
                               format));
    content.append(imageContent(dataUrl, format));

    QJsonArray messages;
    messages.append(messageObject(QStringLiteral("user"), content));

    QJsonObject responseFormat;
    responseFormat.insert(QStringLiteral("type"), QStringLiteral("json_object"));

    QJsonObject root;
    root.insert(QStringLiteral("model"), config.model.trimmed());
    root.insert(QStringLiteral("messages"), messages);
    root.insert(QStringLiteral("response_format"), responseFormat);
    return root;
}

QString VlmAnnotationService::imageToDataUrl(const QString &imagePath,
                                             int maxLongSide,
                                             int jpegQuality,
                                             QString *error)
{
    QImage image(imagePath);
    if (image.isNull()) {
        if (error) {
            *error = QObject::tr("无法读取图片：%1").arg(imagePath);
        }
        return {};
    }

    const int boundedLongSide = qBound(256, maxLongSide, 4096);
    const int currentLongSide = qMax(image.width(), image.height());
    if (currentLongSide > boundedLongSide) {
        image = image.scaled(QSize(boundedLongSide, boundedLongSide),
                             Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
    }

    const bool usePng = image.hasAlphaChannel();
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = QObject::tr("无法创建图片编码缓冲区");
        }
        return {};
    }

    const char *format = usePng ? "PNG" : "JPEG";
    const int quality = usePng ? -1 : qBound(1, jpegQuality, 100);
    if (!image.save(&buffer, format, quality)) {
        if (error) {
            *error = QObject::tr("图片编码失败：%1").arg(imagePath);
        }
        return {};
    }

    return QStringLiteral("data:image/%1;base64,%2")
        .arg(usePng ? QStringLiteral("png") : QStringLiteral("jpeg"),
             QString::fromLatin1(bytes.toBase64()));
}

bool VlmAnnotationService::applyAcceptedResult(ImageMarkManager *manager,
                                               const Task &task,
                                               const QString &category,
                                               const QString &reason)
{
    if (!manager || !ImageMarkManager::isValidCategory(category)) {
        return false;
    }
    if (task.target.folderPath.isEmpty() || task.target.imagePath.isEmpty()) {
        return false;
    }
    return manager->setVlmMarkForImage(task.target.folderPath,
                                       task.target.imagePath,
                                       category,
                                       reason);
}

int VlmAnnotationService::matchRuleToInt(MatchRule rule)
{
    return static_cast<int>(rule);
}

VlmAnnotationService::MatchRule VlmAnnotationService::matchRuleFromInt(int value)
{
    switch (value) {
    case 1:
        return MatchRule::FileName;
    case 2:
        return MatchRule::FileNameFuzzy;
    case 0:
    default:
        return MatchRule::Order;
    }
}

void VlmAnnotationService::startNextBatch()
{
    if (!m_running) {
        return;
    }
    if (m_cancelRequested) {
        if (m_currentReplies.isEmpty()) {
            m_running = false;
            emit progressChanged(m_completed, m_tasks.size(), tr("已取消"));
            emit finished(true);
        }
        return;
    }

    while (m_currentReplies.size() < m_config.concurrency && m_nextIndex < m_tasks.size()) {
        const int batchStart = m_nextIndex;
        const int count = qMin(qMax(1, m_config.batchSize), m_tasks.size() - batchStart);
        m_nextIndex += count;
        sendBatch(batchStart, m_tasks.mid(batchStart, count), 0);
    }

    if (m_currentReplies.isEmpty() && m_completed >= m_tasks.size()) {
        m_running = false;
        emit progressChanged(m_completed, m_tasks.size(), tr("AI 标注完成"));
        emit finished(false);
        return;
    }
}

bool VlmAnnotationService::sendBatch(int batchStart,
                                     const QList<Task> &batch,
                                     int retryCount,
                                     PayloadFormat format)
{
    const QString batchLabel = batch.size() == 1
        ? batch.first().id
        : tr("%1-%2").arg(batch.first().id, batch.last().id);
    const int batchImageCount = imageCountForBatch(batch);
    QStringList batchIds;
    batchIds.reserve(batch.size());
    for (const Task &task : batch) {
        batchIds.append(task.id);
    }
    const auto emitBatchStatus = [this, batchStart, batchIds](const QString &statusText) {
        for (int i = 0; i < batchIds.size(); ++i) {
            emit itemStatusChanged(batchStart + i, batchIds.at(i), statusText);
        }
    };

    emitBatchStatus(tr("构建请求：批次 %1，目标 %2 张，图片输入 %3 张，格式=%4，重试=%5；正在读取/压缩图片并生成 base64 payload")
                        .arg(batchLabel)
                        .arg(batch.size())
                        .arg(batchImageCount)
                        .arg(payloadFormatName(format))
                        .arg(retryCount));
    flushStatusEvents();

    QString payloadError;
    const QJsonObject payload = buildChatCompletionsPayload(m_config, batch, &payloadError, format);
    if (!payloadError.isEmpty()) {
        for (int i = 0; i < batch.size(); ++i) {
            emit itemFailed(batchStart + i, batch.at(i).id, payloadError);
        }
        completeBatch(batchStart, batch);
        return false;
    }

    QStringList expectedIds;
    expectedIds.reserve(batch.size());
    for (const Task &task : batch) {
        expectedIds.append(task.id);
    }

    const QByteArray requestBody = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    const QUrl endpoint = endpointUrl(m_config.baseUrl);
    emitBatchStatus(tr("发送请求：批次 %1，endpoint=%2，payload=%3，图片输入 %4 张；等待上传开始")
                        .arg(batchLabel,
                             endpoint.toString(),
                             byteCountText(requestBody.size()))
                        .arg(batchImageCount));

    QNetworkReply *reply = m_network.post(buildNetworkRequest(m_config), requestBody);
    auto streamState = std::make_shared<StreamingReplyState>();
    m_currentReplies.insert(reply);
    emit progressChanged(m_completed,
                         m_tasks.size(),
                         tr("请求 AI 标注：%1-%2（并发 %3/%4）")
                             .arg(batch.first().id,
                                  batch.last().id)
                             .arg(m_currentReplies.size())
                             .arg(m_config.concurrency));
    emitBatchStatus(tr("等待响应：批次 %1 已发起，payload=%2；若长时间停在这里，通常卡在网络连接、请求上传或服务端排队/推理")
                        .arg(batchLabel, byteCountText(requestBody.size())));
    connect(reply, &QNetworkReply::uploadProgress, this,
            [emitBatchStatus, batchLabel](qint64 bytesSent, qint64 bytesTotal) {
        const QString total = bytesTotal >= 0 ? byteCountText(bytesTotal) : QObject::tr("未知大小");
        emitBatchStatus(QObject::tr("上传中：批次 %1，已上传 %2 / %3")
                            .arg(batchLabel, byteCountText(bytesSent), total));
    });
    connect(reply, &QNetworkReply::downloadProgress, this,
            [emitBatchStatus, batchLabel](qint64 bytesReceived, qint64 bytesTotal) {
        const QString total = bytesTotal >= 0 ? byteCountText(bytesTotal) : QObject::tr("未知大小");
        emitBatchStatus(QObject::tr("接收响应：批次 %1，已接收 %2 / %3")
                            .arg(batchLabel, byteCountText(bytesReceived), total));
    });
    connect(reply, &QNetworkReply::readyRead, this,
            [reply, streamState, emitBatchStatus, batchLabel]() {
        const QByteArray chunk = reply->readAll();
        processStreamingChunk(streamState.get(), chunk);
        if (streamState->sawStreamData) {
            emitBatchStatus(QObject::tr("接收流式响应：批次 %1，原始数据 %2，已聚合 content %3 字符")
                                .arg(batchLabel)
                                .arg(byteCountText(streamState->rawBody.size()))
                                .arg(streamState->content.size()));
        }
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, streamState, batchStart, batch, expectedIds, retryCount, format]() {
        processStreamingChunk(streamState.get(), reply->readAll());
        finalizeStreamingState(streamState.get());
        reply->deleteLater();
        m_currentReplies.remove(reply);

        if (m_cancelRequested) {
            startNextBatch();
            return;
        }

        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = streamState->rawBody;
        const QString batchLabel = batch.size() == 1
            ? batch.first().id
            : tr("%1-%2").arg(batch.first().id, batch.last().id);
        const QString parseStatus = streamState->sawStreamData
            ? tr("解析流式响应：批次 %1，HTTP %2，原始响应 %3，聚合 content %4 字符")
                  .arg(batchLabel)
                  .arg(statusCode)
                  .arg(byteCountText(body.size()))
                  .arg(streamState->content.size())
            : tr("解析响应：批次 %1，HTTP %2，响应体 %3")
                  .arg(batchLabel)
                  .arg(statusCode)
                  .arg(byteCountText(body.size()));
        for (int i = 0; i < batch.size(); ++i) {
            emit itemStatusChanged(batchStart + i,
                                   batch.at(i).id,
                                   parseStatus);
        }

        ParsedResponse parsed;
        if (reply->error() != QNetworkReply::NoError && statusCode == 0) {
            parsed.error = reply->errorString();
        } else if (streamState->sawStreamData) {
            parsed = parseStreamingHttpResponse(statusCode, body, expectedIds);
        } else {
            parsed = parseHttpResponse(statusCode, body, expectedIds);
        }
        if (!parsed.ok
            && format == PayloadFormat::OpenAiContentParts
            && shouldRetryWithDashScopePayload(statusCode, parsed.error)) {
            for (int i = 0; i < batch.size(); ++i) {
                emit itemStatusChanged(batchStart + i,
                                       batch.at(i).id,
                                       tr("准备重试：OpenAI 标准格式被拒绝，切换 DashScope 兼容格式；原因：%1")
                                           .arg(parsed.error.left(300)));
            }
            if (!sendBatch(batchStart, batch, retryCount + 1, PayloadFormat::DashScopeContentParts)) {
                startNextBatch();
            }
            return;
        }
        if (!parsed.ok && statusCode >= 200 && statusCode < 300 && retryCount == 0) {
            for (int i = 0; i < batch.size(); ++i) {
                emit itemStatusChanged(batchStart + i,
                                       batch.at(i).id,
                                       tr("准备重试：HTTP 成功但响应 JSON 不符合预期；原因：%1")
                                           .arg(parsed.error.left(300)));
            }
            if (!sendBatch(batchStart, batch, retryCount + 1, format)) {
                startNextBatch();
            }
            return;
        }

        if (!parsed.ok) {
            for (int i = 0; i < batch.size(); ++i) {
                emit itemFailed(batchStart + i, batch.at(i).id, parsed.error);
            }
            completeBatch(batchStart, batch);
            startNextBatch();
            return;
        }

        for (int i = 0; i < batch.size(); ++i) {
            const QString &id = batch.at(i).id;
            emit itemSucceeded(batchStart + i,
                               id,
                               parsed.categories.value(id),
                               parsed.reasons.value(id));
        }
        completeBatch(batchStart, batch);
        startNextBatch();
    });
    return true;
}

void VlmAnnotationService::completeBatch(int batchStart, const QList<Task> &batch)
{
    Q_UNUSED(batchStart);
    m_completed += batch.size();
    emit progressChanged(m_completed, m_tasks.size(),
                         tr("已处理 %1 / %2").arg(m_completed).arg(m_tasks.size()));
}

QString VlmAnnotationService::builtInFormatPrompt()
{
    return QObject::tr(
        "你是图像分类标注助手。必须只返回一个 JSON 对象，不要输出 Markdown 或额外文字。"
        "JSON 必须包含两个 key：result 和 reason。"
        "result 是对象，key 是目标图片 ID，value 必须是 A、B、C、D、E、F 之一。"
        "reason 是对象，key 是同一个目标图片 ID，value 是该图片分类原因。"
        "必须覆盖所有 TARGET ID，不要为 REFERENCE 图片分类。示例："
        "{\"result\":{\"T0001\":\"A\"},\"reason\":{\"T0001\":\"分类原因\"}}");
}

bool VlmAnnotationService::shouldRetryWithDashScopePayload(int statusCode, const QString &error)
{
    if (statusCode < 400 || statusCode >= 500) {
        return false;
    }
    const QString lower = error.toLower();
    return lower.contains(QStringLiteral("unexpected item type in content"))
        || lower.contains(QStringLiteral("invalidparameter"))
        || lower.contains(QStringLiteral("messages input is invalid"));
}

QString VlmAnnotationService::extractJsonObjectText(const QString &content)
{
    QString trimmed = content.trimmed();
    if (trimmed.startsWith(QStringLiteral("```"))) {
        const int firstBrace = trimmed.indexOf(QLatin1Char('{'));
        const int lastBrace = trimmed.lastIndexOf(QLatin1Char('}'));
        if (firstBrace >= 0 && lastBrace > firstBrace) {
            return trimmed.mid(firstBrace, lastBrace - firstBrace + 1);
        }
    }

    if (trimmed.startsWith(QLatin1Char('{')) && trimmed.endsWith(QLatin1Char('}'))) {
        return trimmed;
    }

    const int firstBrace = trimmed.indexOf(QLatin1Char('{'));
    const int lastBrace = trimmed.lastIndexOf(QLatin1Char('}'));
    if (firstBrace >= 0 && lastBrace > firstBrace) {
        return trimmed.mid(firstBrace, lastBrace - firstBrace + 1);
    }
    return {};
}

int VlmAnnotationService::findMatchIndex(const ColumnSnapshot &column,
                                         const ImageItem &target,
                                         MatchRule rule)
{
    if (column.images.isEmpty()) {
        return -1;
    }

    if (rule == MatchRule::Order) {
        const int row = target.row >= 0 ? target.row : 0;
        return (row >= 0 && row < column.images.size()) ? row : -1;
    }

    if (rule == MatchRule::FileName) {
        for (int i = 0; i < column.images.size(); ++i) {
            if (column.images.at(i).fileName == target.fileName) {
                return i;
            }
        }
        return -1;
    }

    int bestIndex = -1;
    int bestDistance = std::numeric_limits<int>::max();
    const int targetLen = target.fileName.size();
    for (int i = 0; i < column.images.size(); ++i) {
        const QString candidate = column.images.at(i).fileName;
        if (qAbs(targetLen - candidate.size()) >= bestDistance) {
            continue;
        }
        const int distance = levenshteinDistance(target.fileName, candidate);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIndex = i;
            if (distance == 0) {
                break;
            }
        }
    }
    return bestIndex;
}

int VlmAnnotationService::levenshteinDistance(const QString &a, const QString &b)
{
    if (a == b) {
        return 0;
    }
    if (a.isEmpty()) {
        return b.size();
    }
    if (b.isEmpty()) {
        return a.size();
    }

    QVector<int> previous(b.size() + 1);
    QVector<int> current(b.size() + 1);
    for (int j = 0; j <= b.size(); ++j) {
        previous[j] = j;
    }

    for (int i = 1; i <= a.size(); ++i) {
        current[0] = i;
        for (int j = 1; j <= b.size(); ++j) {
            const int cost = (a.at(i - 1) == b.at(j - 1)) ? 0 : 1;
            current[j] = qMin(qMin(current[j - 1] + 1, previous[j] + 1),
                              previous[j - 1] + cost);
        }
        std::swap(previous, current);
    }

    return previous[b.size()];
}
