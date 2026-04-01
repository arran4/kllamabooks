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
                for (const auto& modelVal : modelsArray) {
                    if (modelVal.isObject() && modelVal.toObject().contains("name")) {
                        modelNames.append(modelVal.toObject()["name"].toString());
                    }
                }
                emit modelListUpdated(modelNames);
            }
        } else {
            emit connectionStatusChanged(false);
            emit modelListUpdated(QStringList());  // Clear on error
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

    connect(reply, &QNetworkReply::readyRead, this, [this, reply, modelName]() {
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

    connect(reply, &QNetworkReply::finished, this, [this, reply, modelName]() {
        m_activePulls.remove(reply);
        m_pullBuffers.remove(reply);
        emit pullFinished(modelName);
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
void OllamaClient::generate(const QString& model, const QString& prompt, std::function<void(const QString&)> onChunk,
                            std::function<void(const QString&)> onComplete,
                            std::function<void(const QString&)> onError) {
    QUrl url(m_baseUrl + "/api/generate");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!m_authKey.isEmpty()) {
        request.setRawHeader("Authorization", ("Bearer " + m_authKey).toUtf8());
    }

    QJsonObject json;
    json["model"] = model;
    json["prompt"] = prompt;
    json["stream"] = true;

    if (!m_systemPrompt.isEmpty()) {
        json["system"] = m_systemPrompt;
    }

    QJsonDocument doc(json);
    QByteArray data = doc.toJson();

    emit requestSent(model, m_systemPrompt, prompt);

    QNetworkReply* reply = m_networkManager->post(request, data);

    // Shared pointer to accumulate the full response safely
    auto fullResponse = std::make_shared<QString>();
    // Buffer for incomplete lines
    auto buffer = std::make_shared<QByteArray>();

    connect(reply, &QNetworkReply::readyRead, this, [this, reply, onChunk, fullResponse, buffer]() {
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
                QString responsePart = obj.value("response").toString();
                if (!responsePart.isEmpty()) {
                    fullResponse->append(responsePart);
                    onChunk(responsePart);
                }

                if (obj.value("done").toBool()) {
                    double evalCount = obj.value("eval_count").toDouble();
                    double evalDuration = obj.value("eval_duration").toDouble();
                    if (evalDuration > 0 && evalCount > 0) {
                        double tokensPerSecond = (evalCount / evalDuration) * 1e9;
                        emit generationMetrics(tokensPerSecond);
                    }
                }
            }
        }
    });

    connect(reply, &QNetworkReply::finished, this, [reply, onComplete, onError, fullResponse]() {
        if (reply->error() != QNetworkReply::NoError) {
            onError(reply->errorString());
        } else {
            onComplete(*fullResponse);
        }
        reply->deleteLater();
    });
}

/**
 * @brief Opens a POST stream sequence to the Ollama chat endpoint.
 *
 * @param model The target model hash or tag.
 * @param messages The complete chat history in the format required by Ollama API.
 * @param onChunk Fired per JSON line received.
 * @param onComplete Fired when the JSON EOF boolean is detected.
 * @param onError Fired on socket or HTTP error conditions.
 */
void OllamaClient::generateChat(const QString& model, const QJsonArray& messages,
                                std::function<void(const QString&)> onChunk,
                                std::function<void(const QString&)> onComplete,
                                std::function<void(const QString&)> onError) {
    QUrl url(m_baseUrl + "/api/chat");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!m_authKey.isEmpty()) {
        request.setRawHeader("Authorization", ("Bearer " + m_authKey).toUtf8());
    }

    QJsonObject json;
    json["model"] = model;
    json["messages"] = messages;
    json["stream"] = true;

    if (!m_systemPrompt.isEmpty()) {
        // According to Ollama API documentation, system prompt can be passed either
        // as a top level parameter in chat or injected as a system role message.
        // We will stick to top-level `system` property if they support it, but
        // it is safer to inject it as the first message with role="system".
        QJsonObject sysMsg;
        sysMsg["role"] = "system";
        sysMsg["content"] = m_systemPrompt;

        QJsonArray newMessages;
        newMessages.append(sysMsg);
        for (const auto& msg : messages) {
            newMessages.append(msg);
        }
        json["messages"] = newMessages;
    }

    QJsonDocument doc(json);
    QByteArray data = doc.toJson();

    // Re-use requestSent with stringified prompt to maintain compatibility with GlobalSpy
    QJsonDocument debugDoc(json["messages"].toArray());
    emit requestSent(model, m_systemPrompt, QString(debugDoc.toJson(QJsonDocument::Compact)));

    QNetworkReply* reply = m_networkManager->post(request, data);

    auto fullResponse = std::make_shared<QString>();
    auto buffer = std::make_shared<QByteArray>();

    connect(reply, &QNetworkReply::readyRead, this, [this, reply, onChunk, fullResponse, buffer]() {
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

                // For /api/chat, the actual response string is nested inside "message" object
                QString responsePart;
                if (obj.contains("message") && obj.value("message").isObject()) {
                    responsePart = obj.value("message").toObject().value("content").toString();
                }

                if (!responsePart.isEmpty()) {
                    fullResponse->append(responsePart);
                    onChunk(responsePart);
                }

                if (obj.value("done").toBool()) {
                    double evalCount = obj.value("eval_count").toDouble();
                    double evalDuration = obj.value("eval_duration").toDouble();
                    if (evalDuration > 0 && evalCount > 0) {
                        double tokensPerSecond = (evalCount / evalDuration) * 1e9;
                        emit generationMetrics(tokensPerSecond);
                    }
                }
            }
        }
    });

    connect(reply, &QNetworkReply::finished, this, [reply, onComplete, onError, fullResponse]() {
        if (reply->error() != QNetworkReply::NoError) {
            onError(reply->errorString());
        } else {
            onComplete(*fullResponse);
        }
        reply->deleteLater();
    });
}
