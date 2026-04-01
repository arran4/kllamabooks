#ifndef OLLAMACLIENT_H
#define OLLAMACLIENT_H

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QUrl>
#include <functional>

#include "OllamaModelInfo.h"

class OllamaClient : public QObject {
    Q_OBJECT
   public:
    explicit OllamaClient(QObject* parent = nullptr);
    ~OllamaClient();

    void setBaseUrl(const QString& url);
    void setAuthKey(const QString& key);
    QString getAuthKey() const;
    QString getBaseUrl() const;

    void setSystemPrompt(const QString& prompt);

    void generate(const QString& model, const QString& prompt, std::function<void(const QString&)> onChunk,
                  std::function<void(const QString&)> onComplete, std::function<void(const QString&)> onError);

   signals:
    void connectionStatusChanged(bool isOk);
    void modelListUpdated(const QStringList& models);
    void modelInfoUpdated(const QList<OllamaModelInfo>& models);
    void pullProgressUpdated(const QString& modelName, int percent, const QString& status);
    void pullFinished(const QString& modelName);
    void generationMetrics(double tokensPerSecond);
    void requestSent(const QString& model, const QString& systemPrompt, const QString& prompt);

   public slots:
    void pullModel(const QString& modelName);
    void fetchModels();

   private:
    QNetworkAccessManager* m_networkManager;
    QString m_baseUrl;
    QString m_authKey;
    QString m_systemPrompt;
    QMap<QNetworkReply*, QString> m_activePulls;
    QMap<QNetworkReply*, QByteArray> m_pullBuffers;
};

#endif  // OLLAMACLIENT_H
