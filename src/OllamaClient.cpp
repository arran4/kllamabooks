#include "OllamaClient.h"

#include <QEventLoop>
#include <QMap>
#include <QNetworkRequest>
#include <QTimer>

OllamaClient::OllamaClient(QObject* parent)
    : QObject(parent),
      m_networkManager(new QNetworkAccessManager(this)),
      m_baseUrl("http://localhost:11434"),
      m_authKey("") {}

OllamaClient::~OllamaClient() {}

void OllamaClient::setBaseUrl(const QString& url) { m_baseUrl = url; }

void OllamaClient::setAuthKey(const QString& key) { m_authKey = key; }

QString OllamaClient::getAuthKey() const { return m_authKey; }

QString OllamaClient::getBaseUrl() const { return m_baseUrl; }

void OllamaClient::setSystemPrompt(const QString& prompt) { m_systemPrompt = prompt; }

/**
 * @brief Issues a REST GET to discover installed model tags on the local Ollama daemon.
 */
void OllamaClient::fetchModels() {
    QUrl url(m_baseUrl + "/api/tags");
    QNetworkRequest request(url);
    if (!m_authKey.isEmpty()) {
        request.setRawHeader("Authorization", ("Bearer " + m_authKey).toUtf8());
    }
    QNetworkReply* reply = m_networkManager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            emit connectionStatusChanged(true);
            QByteArray response = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(response);
            if (doc.isObject() && doc.object().contains("models") && doc.object()["models"].isArray()) {
                QJsonArray modelsArray = doc.object()["models"].toArray();
                QStringList modelNames;
                QList<OllamaModelInfo> modelInfos;
                for (const auto& modelVal : modelsArray) {
                    if (modelVal.isObject() && modelVal.toObject().contains("name")) {
                        QJsonObject modelObj = modelVal.toObject();
                        QString name = modelObj["name"].toString();
                        modelNames.append(name);

                        OllamaModelInfo info;
                        info.name = name;

                        // Parse size
                        if (modelObj.contains("size")) {
                            qint64 sizeBytes = modelObj["size"].toVariant().toLongLong();
                            if (sizeBytes > 1024LL * 1024LL * 1024LL) {
                                info.size = QString::number(sizeBytes / (1024.0 * 1024.0 * 1024.0), 'f', 1) + " GB";
                            } else if (sizeBytes > 1024 * 1024) {
                                info.size = QString::number(sizeBytes / (1024.0 * 1024.0), 'f', 1) + " MB";
                            } else {
                                info.size = QString::number(sizeBytes) + " B";
                            }
                        }

                        // Parse details
                        if (modelObj.contains("details") && modelObj["details"].isObject()) {
                            QJsonObject detailsObj = modelObj["details"].toObject();
                            info.family = detailsObj["family"].toString();
                            info.parameterSize = detailsObj["parameter_size"].toString();
                            info.quantizationLevel = detailsObj["quantization_level"].toString();
                        }

                        modelInfos.append(info);
                    }
                }
                emit modelListUpdated(modelNames);
                emit modelInfoUpdated(modelInfos);
            }
        } else {
            emit connectionStatusChanged(false);
            emit modelListUpdated(QStringList());  // Clear on error
            emit modelInfoUpdated(QList<OllamaModelInfo>());
        }
        reply->deleteLater();
    });
}

/**
 * @brief Issues a REST POST to request downloading a model from the public Ollama registry.
 *
 * @param modelName The registry namespace tag.
 */
void OllamaClient::pullModel(const QString& modelName) {
    QUrl url(m_baseUrl + "/api/pull");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!m_authKey.isEmpty()) {
        request.setRawHeader("Authorization", ("Bearer " + m_authKey).toUtf8());
    }

    QJsonObject json;
    json["name"] = modelName;
    json["stream"] = true;

    QJsonDocument doc(json);
    QByteArray data = doc.toJson();

    QNetworkReply* reply = m_networkManager->post(request, data);
    m_activePulls[reply] = modelName;
    m_pullBuffers[reply] = QByteArray();

    // We'll track if there was an error in the streaming JSON
    auto hasError = std::make_shared<bool>(false);
    auto errorMsg = std::make_shared<QString>("");

    connect(reply, &QNetworkReply::readyRead, this, [this, reply, modelName, hasError, errorMsg]() {
        m_pullBuffers[reply].append(reply->readAll());
        QByteArray& buffer = m_pullBuffers[reply];

        while (true) {
            int newlineIndex = buffer.indexOf('\n');
            if (newlineIndex == -1) break;

            QByteArray line = buffer.left(newlineIndex);
            buffer.remove(0, newlineIndex + 1);

            if (line.trimmed().isEmpty()) continue;

            QJsonParseError error;
            QJsonDocument doc = QJsonDocument::fromJson(line, &error);
            if (error.error == QJsonParseError::NoError && doc.isObject()) {
                QJsonObject obj = doc.object();

                if (obj.contains("error")) {
                    *hasError = true;
                    *errorMsg = obj.value("error").toString();
                    emit pullProgressUpdated(modelName, 0, "Error: " + *errorMsg);
                    return;
                }

                QString status = obj.value("status").toString();
                qint64 total = obj.value("total").toDouble();
                qint64 completed = obj.value("completed").toDouble();
                int percent = 0;
                if (total > 0) {
                    percent = static_cast<int>((completed * 100.0) / total);
                } else if (status == "success") {
                    percent = 100;
                }
                emit pullProgressUpdated(modelName, percent, status);
            }
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, modelName, hasError, errorMsg]() {
        m_activePulls.remove(reply);
        m_pullBuffers.remove(reply);

        if (reply->error() != QNetworkReply::NoError) {
            emit pullFinished(modelName, false, reply->errorString());
        } else if (*hasError) {
            emit pullFinished(modelName, false, *errorMsg);
        } else {
            emit pullFinished(modelName, true);
        }

        fetchModels();
        reply->deleteLater();
    });
}

/**
 * @brief Opens a POST stream sequence to the Ollama generate endpoint.
 *
 * @param model The target model hash or tag.
 * @param prompt The complete inference string.
 * @param onChunk Fired per JSON line received.
 * @param onComplete Fired when the JSON EOF boolean is detected.
 * @param onError Fired on socket or HTTP error conditions.
 */
namespace {

template <typename Extractor>
void attachStreamingHandlers(OllamaClient* client, QNetworkReply* reply, const llm::LlmCallbacks& callbacks,
                             QList<QNetworkReply*>& activeGenerations, Extractor&& extractPart) {
    auto fullResponse = std::make_shared<QString>();
    auto buffer = std::make_shared<QByteArray>();

    QObject::connect(reply, &QNetworkReply::readyRead, client,
                     [client, reply, callbacks, fullResponse, buffer, extractPart]() {
                         buffer->append(reply->readAll());

                         while (true) {
                             int newlineIndex = buffer->indexOf('\n');
                             if (newlineIndex == -1) break;

                             QByteArray line = buffer->left(newlineIndex);
                             buffer->remove(0, newlineIndex + 1);

                             if (line.trimmed().isEmpty()) continue;

                             QJsonParseError error;
                             QJsonDocument doc = QJsonDocument::fromJson(line, &error);
                             if (error.error == QJsonParseError::NoError && doc.isObject()) {
                                 QJsonObject obj = doc.object();
                                 QString responsePart = extractPart(obj);
                                 if (!responsePart.isEmpty()) {
                                     fullResponse->append(responsePart);
                                     if (callbacks.onChunk) callbacks.onChunk(responsePart);
                                 }

                                 if (obj.value("done").toBool()) {
                                     double evalCount = obj.value("eval_count").toDouble();
                                     double evalDuration = obj.value("eval_duration").toDouble();
                                     if (evalDuration > 0 && evalCount > 0) {
                                         double tokensPerSecond = (evalCount / evalDuration) * 1e9;
                                         emit client->generationMetrics(tokensPerSecond);
                                     }
                                 }
                             }
                         }
                     });

    QObject::connect(reply, &QNetworkReply::finished, client,
                     [client, reply, callbacks, fullResponse, &activeGenerations]() {
                         activeGenerations.removeAll(reply);
                         if (reply->error() != QNetworkReply::NoError) {
                             emit client->connectionStatusChanged(false);
                             if (callbacks.onError) callbacks.onError(reply->error(), reply->errorString());
                         } else {
                             if (callbacks.onComplete) callbacks.onComplete(*fullResponse);
                         }
                         reply->deleteLater();
                     });
}

}  // namespace

QNetworkReply* OllamaClient::generate(const llm::LlmRequestOptions& options, const QString& prompt,
                                      const llm::LlmCallbacks& callbacks) {
    QUrl url(m_baseUrl + "/api/generate");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!m_authKey.isEmpty()) {
        request.setRawHeader("Authorization", ("Bearer " + m_authKey).toUtf8());
    }

    const QJsonObject json = llm::buildOllamaGeneratePayload(options, prompt);
    const QByteArray data = QJsonDocument(json).toJson();

    emit requestSent(options.model, options.systemPrompt, prompt);

    QNetworkReply* reply = m_networkManager->post(request, data);
    m_activeGenerations.append(reply);

    attachStreamingHandlers(this, reply, callbacks, m_activeGenerations,
                            [](const QJsonObject& obj) { return obj.value("response").toString(); });
    return reply;
}

QNetworkReply* OllamaClient::generate(const QString& model, const QString& prompt,
                                      std::function<void(const QString&)> onChunk,
                                      std::function<void(const QString&)> onComplete,
                                      std::function<void(QNetworkReply::NetworkError, const QString&)> onError) {
    return generate(llm::LlmRequestOptions{model, m_systemPrompt, {}}, prompt,
                    llm::LlmCallbacks{std::move(onChunk), std::move(onComplete), std::move(onError)});
}

/**
 * @brief Opens a POST stream sequence to the Ollama chat endpoint.
 *
 * @param options Per-request model, system prompt, and generation options.
 * @param messages The complete chat history in the format required by Ollama API.
 * @param callbacks Streaming callbacks.
 */
QNetworkReply* OllamaClient::generateChat(const llm::LlmRequestOptions& options, const QJsonArray& messages,
                                          const llm::LlmCallbacks& callbacks) {
    QUrl url(m_baseUrl + "/api/chat");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!m_authKey.isEmpty()) {
        request.setRawHeader("Authorization", ("Bearer " + m_authKey).toUtf8());
    }

    const QJsonObject json = llm::buildOllamaChatPayload(options, messages);
    const QByteArray data = QJsonDocument(json).toJson();

    // Re-use requestSent with stringified prompt to maintain compatibility with GlobalSpy
    QJsonDocument debugDoc(json["messages"].toArray());
    emit requestSent(options.model, options.systemPrompt, QString(debugDoc.toJson(QJsonDocument::Compact)));

    QNetworkReply* reply = m_networkManager->post(request, data);
    m_activeGenerations.append(reply);

    attachStreamingHandlers(this, reply, callbacks, m_activeGenerations, [](const QJsonObject& obj) {
        if (obj.contains("message") && obj.value("message").isObject()) {
            return obj.value("message").toObject().value("content").toString();
        }
        return QString();
    });
    return reply;
}

QNetworkReply* OllamaClient::generateChat(const QString& model, const QJsonArray& messages,
                                          std::function<void(const QString&)> onChunk,
                                          std::function<void(const QString&)> onComplete,
                                          std::function<void(QNetworkReply::NetworkError, const QString&)> onError) {
    return generateChat(llm::LlmRequestOptions{model, m_systemPrompt, {}}, messages,
                        llm::LlmCallbacks{std::move(onChunk), std::move(onComplete), std::move(onError)});
}

void OllamaClient::abortGenerations() {
    const auto activeGens = m_activeGenerations;
    for (QNetworkReply* reply : activeGens) {
        reply->abort();
    }
    // They will be removed from m_activeGenerations in the finished slot
}
