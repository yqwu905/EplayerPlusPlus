#include "VlmAnnotationService.h"
#include "ImageMarkManager.h"

#include <QBuffer>
#include <QByteArray>
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

    QNetworkReply *reply = m_network.post(buildNetworkRequest(m_config),
                                          QJsonDocument(payload).toJson(QJsonDocument::Compact));
    m_currentReplies.insert(reply);
    emit progressChanged(m_completed,
                         m_tasks.size(),
                         tr("请求 AI 标注：%1-%2（并发 %3/%4）")
                             .arg(batch.first().id,
                                  batch.last().id)
                             .arg(m_currentReplies.size())
                             .arg(m_config.concurrency));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, batchStart, batch, expectedIds, retryCount, format]() {
        reply->deleteLater();
        m_currentReplies.remove(reply);

        if (m_cancelRequested) {
            startNextBatch();
            return;
        }

        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        ParsedResponse parsed;
        if (reply->error() != QNetworkReply::NoError && statusCode == 0) {
            parsed.error = reply->errorString();
        } else {
            parsed = parseHttpResponse(statusCode, body, expectedIds);
        }
        if (!parsed.ok
            && format == PayloadFormat::OpenAiContentParts
            && shouldRetryWithDashScopePayload(statusCode, parsed.error)) {
            if (!sendBatch(batchStart, batch, retryCount + 1, PayloadFormat::DashScopeContentParts)) {
                startNextBatch();
            }
            return;
        }
        if (!parsed.ok && statusCode >= 200 && statusCode < 300 && retryCount == 0) {
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
