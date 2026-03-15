#ifndef OLLAMACLIENT_H
#define OLLAMACLIENT_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <functional>

class OllamaClient : public QObject {
    Q_OBJECT
public:
    explicit OllamaClient(QObject *parent = nullptr);
    ~OllamaClient();

    void setBaseUrl(const QString& url);
    QString getBaseUrl() const;

    void generate(const QString& model, const QString& prompt,
                  std::function<void(const QString&)> onChunk,
                  std::function<void(const QString&)> onComplete,
                  std::function<void(const QString&)> onError);

signals:
    void modelListUpdated(const QStringList& models);

public slots:
    void fetchModels();

private:
    QNetworkAccessManager* m_networkManager;
    QString m_baseUrl;
};

#endif // OLLAMACLIENT_H
