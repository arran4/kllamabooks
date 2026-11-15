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
#include "llm/LlmProvider.h"

class OllamaClient : public QObject, public llm::LlmProvider {
    Q_OBJECT
   public:
    explicit OllamaClient(QObject* parent = nullptr);
    virtual ~OllamaClient();

    QString providerName() const override { return QStringLiteral("ollama"); }

    void setBaseUrl(const QString& url);
    void setAuthKey(const QString& key);
    QString getAuthKey() const;
    QString getBaseUrl() const;

    void setSystemPrompt(const QString& prompt);

    QNetworkReply* generate(const llm::LlmRequestOptions& options, const QString& prompt,
                            const llm::LlmCallbacks& callbacks) override;

    QNetworkReply* generateChat(const llm::LlmRequestOptions& options, const QJsonArray& messages,
                                const llm::LlmCallbacks& callbacks) override;

    virtual QNetworkReply* generate(const QString& model, const QString& prompt,
                                    std::function<void(const QString&)> onChunk,
                                    std::function<void(const QString&)> onComplete,
                                    std::function<void(QNetworkReply::NetworkError, const QString&)> onError);

    virtual QNetworkReply* generateChat(const QString& model, const QJsonArray& messages,
                                        std::function<void(const QString&)> onChunk,
                                        std::function<void(const QString&)> onComplete,
                                        std::function<void(QNetworkReply::NetworkError, const QString&)> onError);

    void abortGenerations() override;

   signals:
    void connectionStatusChanged(bool isOk);
    void modelListUpdated(const QStringList& models);
    void modelInfoUpdated(const QList<OllamaModelInfo>& models);
    void pullProgressUpdated(const QString& modelName, int percent, const QString& status);
    void pullFinished(const QString& modelName, bool success, const QString& errorString = "");
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
    QList<QNetworkReply*> m_activeGenerations;
};

#endif  // OLLAMACLIENT_H
