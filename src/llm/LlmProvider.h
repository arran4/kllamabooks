#ifndef LLM_LLMPROVIDER_H
#define LLM_LLMPROVIDER_H

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkReply>
#include <QString>
#include <functional>

namespace llm {

/**
 * @brief Immutable per-request options passed to an LLM provider.
 *
 * Keeps model, system prompt, and generation parameters scoped to a single
 * request so concurrent generations never share mutable client state.
 */
struct LlmRequestOptions {
    QString model;
    QString systemPrompt;
    QJsonObject optionsJson;
};

/**
 * @brief Streaming callbacks for an asynchronous LLM generation request.
 */
struct LlmCallbacks {
    std::function<void(const QString&)> onChunk;
    std::function<void(const QString&)> onComplete;
    std::function<void(QNetworkReply::NetworkError, const QString&)> onError;
};

/**
 * @brief Builds the JSON request body for Ollama's /api/generate endpoint.
 */
inline QJsonObject buildOllamaGeneratePayload(const LlmRequestOptions& options, const QString& prompt) {
    QJsonObject json;
    json["model"] = options.model;
    json["prompt"] = prompt;
    json["stream"] = true;
    if (!options.systemPrompt.isEmpty()) {
        json["system"] = options.systemPrompt;
    }
    if (!options.optionsJson.isEmpty()) {
        json["options"] = options.optionsJson;
    }
    return json;
}

/**
 * @brief Builds the JSON request body for Ollama's /api/chat endpoint.
 */
inline QJsonObject buildOllamaChatPayload(const LlmRequestOptions& options, const QJsonArray& messages) {
    QJsonObject json;
    json["model"] = options.model;
    json["stream"] = true;

    if (!options.systemPrompt.isEmpty()) {
        QJsonObject sysMsg;
        sysMsg["role"] = QStringLiteral("system");
        sysMsg["content"] = options.systemPrompt;

        QJsonArray withSystem;
        withSystem.append(sysMsg);
        for (const auto& msg : messages) {
            withSystem.append(msg);
        }
        json["messages"] = withSystem;
    } else {
        json["messages"] = messages;
    }

    if (!options.optionsJson.isEmpty()) {
        json["options"] = options.optionsJson;
    }
    return json;
}

/**
 * @brief Abstract interface for LLM inference backends.
 */
class LlmProvider {
   public:
    virtual ~LlmProvider() = default;

    virtual QString providerName() const = 0;

    virtual QNetworkReply* generate(const LlmRequestOptions& options, const QString& prompt,
                                    const LlmCallbacks& callbacks) = 0;

    virtual QNetworkReply* generateChat(const LlmRequestOptions& options, const QJsonArray& messages,
                                        const LlmCallbacks& callbacks) = 0;

    virtual void abortGenerations() = 0;
};

}  // namespace llm

#endif  // LLM_LLMPROVIDER_H
