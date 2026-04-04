#ifndef QUEUEMANAGER_H
#define QUEUEMANAGER_H

#include <QList>
#include <QObject>
#include <QSettings>
#include <QTimer>

#include "BookDatabase.h"
#include "OllamaClient.h"

class QueueManager : public QObject {
    Q_OBJECT
   public:
    static QueueManager& instance();

    void addDatabase(std::shared_ptr<BookDatabase> db);
    void removeDatabase(std::shared_ptr<BookDatabase> db);
    void setActiveDatabase(std::shared_ptr<BookDatabase> db);
    QList<std::shared_ptr<BookDatabase>> databases() const { return m_databases; }

    void setClient(OllamaClient* client);
    OllamaClient* client() const { return m_client; }
    void enqueuePrompt(int messageId, const QString& model, const QString& prompt, int priority = 0,
                       const QString& targetType = "message", int parentId = 0, const QString& targetAction = "");

    int totalPendingCount() const;
    int pendingCount(std::shared_ptr<BookDatabase> db) const;

    struct MergedQueueItem {
        std::shared_ptr<BookDatabase> db;
        QueueItem item;
    };
    QList<MergedQueueItem> getMergedQueue() const;

    void cancelItem(std::shared_ptr<BookDatabase> db, int queueId);
    void retryItem(std::shared_ptr<BookDatabase> db, int queueId);
    void modifyItem(std::shared_ptr<BookDatabase> db, int queueId, const QString& newPrompt, const QString& newModel = QString());

    void clearCompleted();
    void pauseQueue();
    void resumeQueue();
    bool isPaused() const { return m_isPaused; }

    bool isEndpointUp() const { return m_isEndpointUp; }

    bool isProcessing() const { return !m_activeItems.isEmpty(); }
    void setMaxConcurrent(int max);
    int maxConcurrent() const { return m_maxConcurrent; }
    QList<MergedQueueItem> currentProcessingItems() const { return m_activeItems.values(); }

   public slots:
    void checkQueue();

   signals:
    void queueChanged();
    void processingStarted(std::shared_ptr<BookDatabase> db, int messageId, const QString& targetType = "message");
    void processingChunk(std::shared_ptr<BookDatabase> db, int messageId, const QString& chunk,
                         const QString& targetType = "message");
    void processingFinished(std::shared_ptr<BookDatabase> db, int messageId, bool success,
                            const QString& targetType = "message");



   private:
    explicit QueueManager(QObject* parent = nullptr);
    ~QueueManager() = default;

    QList<std::shared_ptr<BookDatabase>> m_databases;
    std::shared_ptr<BookDatabase> m_activeDb = nullptr;
    OllamaClient* m_client = nullptr;
    QTimer* m_timer;

    QMap<int, MergedQueueItem> m_activeItems; // Map processingId to QueueItem
    int m_maxConcurrent = 1;
    bool m_isPaused = false;
    QString m_lastProcessedModel;

    int m_nextProcessingId = 1; // Counter to track unique processing IDs
    bool m_isEndpointUp = true;
    QTimer* m_probeTimer;

    void processNext();
};

#endif  // QUEUEMANAGER_H
