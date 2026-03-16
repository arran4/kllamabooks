#include "OllamaClient.h"
#include <QNetworkRequest>
#include <QTimer>
#include <QEventLoop>

OllamaClient::OllamaClient(QObject *parent)
    : QObject(parent), m_networkManager(new QNetworkAccessManager(this)), m_baseUrl("http://localhost:11434") {
}

OllamaClient::~OllamaClient() {
}

void OllamaClient::setBaseUrl(const QString& url) {
    m_baseUrl = url;
}

QString OllamaClient::getBaseUrl() const {
    return m_baseUrl;
}

void OllamaClient::fetchModels() {
    QUrl url(m_baseUrl + "/api/tags");
    QNetworkRequest request(url);
    QNetworkReply *reply = m_networkManager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
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
        }
        reply->deleteLater();
    });
}

void OllamaClient::generate(const QString& model, const QString& prompt,
                            std::function<void(const QString&)> onChunk,
                            std::function<void(const QString&)> onComplete,
                            std::function<void(const QString&)> onError) {
    QUrl url(m_baseUrl + "/api/generate");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject json;
    json["model"] = model;
    json["prompt"] = prompt;
    json["stream"] = true;

    QJsonDocument doc(json);
    QByteArray data = doc.toJson();

    QNetworkReply *reply = m_networkManager->post(request, data);

    connect(reply, &QNetworkReply::readyRead, this, [reply, onChunk]() {
        QByteArray chunk = reply->readAll();
        // The stream gives us multiple JSON objects, often separated by newlines
        QList<QByteArray> lines = chunk.split('\n');
        for (const QByteArray& line : lines) {
            if (line.trimmed().isEmpty()) continue;
            QJsonParseError error;
            QJsonDocument doc = QJsonDocument::fromJson(line, &error);
            if (error.error == QJsonParseError::NoError && doc.isObject()) {
                QString responsePart = doc.object().value("response").toString();
                if (!responsePart.isEmpty()) {
                    onChunk(responsePart);
                }
            }
        }
    });

    connect(reply, &QNetworkReply::finished, this, [reply, onComplete, onError]() {
        if (reply->error() != QNetworkReply::NoError) {
            onError(reply->errorString());
        } else {
            onComplete(""); // Pass empty string or full content if needed later
        }
        reply->deleteLater();
    });
}
