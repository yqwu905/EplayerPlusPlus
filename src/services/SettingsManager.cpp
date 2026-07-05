#include "SettingsManager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QUuid>
#include <QVariant>
#include <algorithm>

const QString SettingsManager::kFolderListKey = QStringLiteral("folders/list");
const QString SettingsManager::kComparisonThresholdKey = QStringLiteral("comparison/threshold");
const QString SettingsManager::kResizeToFirstImageKey = QStringLiteral("comparison/resize_to_first_image");
const QString SettingsManager::kIgnoreImageColorProfileKey = QStringLiteral("image/ignore_color_profile");
const QString SettingsManager::kSplitterSizesKey = QStringLiteral("ui/splitter_sizes");
const QString SettingsManager::kVlmBaseUrlKey = QStringLiteral("vlm/base_url");
const QString SettingsManager::kVlmModelKey = QStringLiteral("vlm/model");
const QString SettingsManager::kVlmUserPromptKey = QStringLiteral("vlm/user_prompt");
const QString SettingsManager::kVlmMatchRuleKey = QStringLiteral("vlm/match_rule");
const QString SettingsManager::kVlmMaxItemsKey = QStringLiteral("vlm/max_items");
const QString SettingsManager::kVlmConcurrencyKey = QStringLiteral("vlm/concurrency");
const QString SettingsManager::kVlmSkipMarkedKey = QStringLiteral("vlm/skip_marked");
const QString SettingsManager::kVlmRememberApiKeyKey = QStringLiteral("vlm/remember_api_key");
const QString SettingsManager::kVlmApiKeyKey = QStringLiteral("vlm/api_key");
const QString SettingsManager::kVlmProvidersKey = QStringLiteral("vlm/providers");
const QString SettingsManager::kVlmActiveProviderIdKey = QStringLiteral("vlm/active_provider_id");

SettingsManager::SettingsManager(QObject *parent)
    : QObject(parent)
{
}

SettingsManager::~SettingsManager() = default;

QStringList SettingsManager::loadFolderList() const
{
    QSettings settings;
    return settings.value(kFolderListKey).toStringList();
}

void SettingsManager::saveFolderList(const QStringList &folders)
{
    QSettings settings;
    settings.setValue(kFolderListKey, folders);
}

void SettingsManager::addFolder(const QString &folderPath)
{
    QStringList folders = loadFolderList();
    if (!folders.contains(folderPath)) {
        folders.append(folderPath);
        saveFolderList(folders);
    }
}

void SettingsManager::removeFolder(const QString &folderPath)
{
    QStringList folders = loadFolderList();
    folders.removeAll(folderPath);
    saveFolderList(folders);
}

void SettingsManager::clearFolderList()
{
    QSettings settings;
    settings.remove(kFolderListKey);
}

int SettingsManager::comparisonThreshold() const
{
    QSettings settings;
    return std::clamp(settings.value(kComparisonThresholdKey, 10).toInt(), 0, 255);
}

void SettingsManager::setComparisonThreshold(int threshold)
{
    QSettings settings;
    settings.setValue(kComparisonThresholdKey, std::clamp(threshold, 0, 255));
}

bool SettingsManager::resizeToFirstImageEnabled() const
{
    QSettings settings;
    return settings.value(kResizeToFirstImageKey, false).toBool();
}

void SettingsManager::setResizeToFirstImageEnabled(bool enabled)
{
    QSettings settings;
    settings.setValue(kResizeToFirstImageKey, enabled);
}

bool SettingsManager::ignoreImageColorProfile() const
{
    QSettings settings;
    // Default ON: a pixel-diff tool wants two visually-identical files with
    // different color profiles to compare equal. Users who care about
    // color-managed rendering can opt out.
    return settings.value(kIgnoreImageColorProfileKey, true).toBool();
}

void SettingsManager::setIgnoreImageColorProfile(bool enabled)
{
    QSettings settings;
    settings.setValue(kIgnoreImageColorProfileKey, enabled);
}

QList<int> SettingsManager::splitterSizes() const
{
    QSettings settings;
    const QVariantList stored = settings.value(kSplitterSizesKey).toList();
    QList<int> sizes;
    sizes.reserve(stored.size());
    for (const QVariant &value : stored) {
        sizes.append(value.toInt());
    }
    return sizes;
}

void SettingsManager::setSplitterSizes(const QList<int> &sizes)
{
    QVariantList stored;
    stored.reserve(sizes.size());
    for (int size : sizes) {
        stored.append(size);
    }
    QSettings settings;
    settings.setValue(kSplitterSizesKey, stored);
}

QString SettingsManager::vlmBaseUrl() const
{
    QSettings settings;
    return settings.value(kVlmBaseUrlKey, QStringLiteral("https://api.openai.com/v1")).toString();
}

void SettingsManager::setVlmBaseUrl(const QString &baseUrl)
{
    QSettings settings;
    settings.setValue(kVlmBaseUrlKey, baseUrl.trimmed());
}

QString SettingsManager::vlmModel() const
{
    QSettings settings;
    return settings.value(kVlmModelKey).toString();
}

void SettingsManager::setVlmModel(const QString &model)
{
    QSettings settings;
    settings.setValue(kVlmModelKey, model.trimmed());
}

QString SettingsManager::vlmUserPrompt() const
{
    QSettings settings;
    return settings.value(kVlmUserPromptKey).toString();
}

void SettingsManager::setVlmUserPrompt(const QString &prompt)
{
    QSettings settings;
    settings.setValue(kVlmUserPromptKey, prompt);
}

int SettingsManager::vlmMatchRule() const
{
    QSettings settings;
    return std::clamp(settings.value(kVlmMatchRuleKey, 0).toInt(), 0, 2);
}

void SettingsManager::setVlmMatchRule(int rule)
{
    QSettings settings;
    settings.setValue(kVlmMatchRuleKey, std::clamp(rule, 0, 2));
}

int SettingsManager::vlmMaxItems() const
{
    QSettings settings;
    return std::max(0, settings.value(kVlmMaxItemsKey, 0).toInt());
}

void SettingsManager::setVlmMaxItems(int maxItems)
{
    QSettings settings;
    settings.setValue(kVlmMaxItemsKey, std::max(0, maxItems));
}

int SettingsManager::vlmConcurrency() const
{
    QSettings settings;
    return std::clamp(settings.value(kVlmConcurrencyKey, 1).toInt(), 1, 16);
}

void SettingsManager::setVlmConcurrency(int concurrency)
{
    QSettings settings;
    settings.setValue(kVlmConcurrencyKey, std::clamp(concurrency, 1, 16));
}

bool SettingsManager::vlmSkipMarked() const
{
    QSettings settings;
    return settings.value(kVlmSkipMarkedKey, true).toBool();
}

void SettingsManager::setVlmSkipMarked(bool enabled)
{
    QSettings settings;
    settings.setValue(kVlmSkipMarkedKey, enabled);
}

bool SettingsManager::vlmRememberApiKey() const
{
    QSettings settings;
    return settings.value(kVlmRememberApiKeyKey, false).toBool();
}

void SettingsManager::setVlmRememberApiKey(bool enabled)
{
    QSettings settings;
    settings.setValue(kVlmRememberApiKeyKey, enabled);
    if (!enabled) {
        settings.remove(kVlmApiKeyKey);
    }
}

QString SettingsManager::vlmApiKey() const
{
    QSettings settings;
    if (!settings.value(kVlmRememberApiKeyKey, false).toBool()) {
        return QString();
    }
    return settings.value(kVlmApiKeyKey).toString();
}

void SettingsManager::setVlmApiKey(const QString &apiKey)
{
    QSettings settings;
    if (!settings.value(kVlmRememberApiKeyKey, false).toBool()) {
        settings.remove(kVlmApiKeyKey);
        return;
    }
    settings.setValue(kVlmApiKeyKey, apiKey);
}

QList<SettingsManager::VlmProvider> SettingsManager::vlmProviders() const
{
    QSettings settings;
    QList<VlmProvider> providers;

    const QByteArray payload = settings.value(kVlmProvidersKey).toByteArray();
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (document.isArray()) {
        const QJsonArray array = document.array();
        providers.reserve(array.size());
        for (const QJsonValue &value : array) {
            const QJsonObject object = value.toObject();
            VlmProvider provider;
            provider.id = object.value(QStringLiteral("id")).toString().trimmed();
            provider.name = object.value(QStringLiteral("name")).toString().trimmed();
            provider.baseUrl = object.value(QStringLiteral("baseUrl")).toString().trimmed();
            provider.model = object.value(QStringLiteral("model")).toString().trimmed();
            provider.apiKey = object.value(QStringLiteral("apiKey")).toString();
            if (provider.id.isEmpty()) {
                provider.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            }
            if (provider.name.isEmpty()) {
                provider.name = provider.model.isEmpty()
                    ? QStringLiteral("VLM Provider")
                    : provider.model;
            }
            if (provider.baseUrl.isEmpty()) {
                provider.baseUrl = QStringLiteral("https://api.openai.com/v1");
            }
            providers.append(provider);
        }
    }

    // One-time compatibility path for the first VLM implementation, where the
    // API fields lived directly in the annotation dialog settings.
    if (providers.isEmpty()) {
        const QString legacyModel = vlmModel();
        const QString legacyApiKey = vlmApiKey();
        if (!legacyModel.isEmpty() || !legacyApiKey.isEmpty()) {
            VlmProvider provider;
            provider.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            provider.name = legacyModel.isEmpty() ? QStringLiteral("默认 Provider") : legacyModel;
            provider.baseUrl = vlmBaseUrl();
            provider.model = legacyModel;
            provider.apiKey = legacyApiKey;
            providers.append(provider);
        }
    }

    return providers;
}

void SettingsManager::setVlmProviders(const QList<VlmProvider> &providers)
{
    QJsonArray array;
    QStringList ids;
    ids.reserve(providers.size());
    for (VlmProvider provider : providers) {
        provider.id = provider.id.trimmed();
        if (provider.id.isEmpty()) {
            provider.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }
        provider.name = provider.name.trimmed();
        provider.baseUrl = provider.baseUrl.trimmed();
        provider.model = provider.model.trimmed();
        if (provider.name.isEmpty() || provider.baseUrl.isEmpty() || provider.model.isEmpty()) {
            continue;
        }

        QJsonObject object;
        object.insert(QStringLiteral("id"), provider.id);
        object.insert(QStringLiteral("name"), provider.name);
        object.insert(QStringLiteral("baseUrl"), provider.baseUrl);
        object.insert(QStringLiteral("model"), provider.model);
        object.insert(QStringLiteral("apiKey"), provider.apiKey);
        array.append(object);
        ids.append(provider.id);
    }

    QSettings settings;
    settings.setValue(kVlmProvidersKey, QJsonDocument(array).toJson(QJsonDocument::Compact));

    const QString activeId = settings.value(kVlmActiveProviderIdKey).toString();
    if (!ids.contains(activeId)) {
        if (ids.isEmpty()) {
            settings.remove(kVlmActiveProviderIdKey);
        } else {
            settings.setValue(kVlmActiveProviderIdKey, ids.first());
        }
    }
}

QString SettingsManager::activeVlmProviderId() const
{
    QSettings settings;
    const QList<VlmProvider> providers = vlmProviders();
    const QString stored = settings.value(kVlmActiveProviderIdKey).toString();
    for (const VlmProvider &provider : providers) {
        if (provider.id == stored) {
            return stored;
        }
    }
    return providers.isEmpty() ? QString() : providers.first().id;
}

void SettingsManager::setActiveVlmProviderId(const QString &providerId)
{
    QSettings settings;
    settings.setValue(kVlmActiveProviderIdKey, providerId);
}

SettingsManager::VlmProvider SettingsManager::activeVlmProvider() const
{
    const QString activeId = activeVlmProviderId();
    const QList<VlmProvider> providers = vlmProviders();
    for (const VlmProvider &provider : providers) {
        if (provider.id == activeId) {
            return provider;
        }
    }
    return providers.isEmpty() ? VlmProvider{} : providers.first();
}
