#ifndef PROMPTQUEUEMANAGER_H
#define PROMPTQUEUEMANAGER_H

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

#include "OllamaClient.h"

struct QueueJob {
    int id;
    QString bookFilepath;
    QString model;
    QString systemPrompt;
    QString promptText;
    int parentMessageId;
    int userMessageId;
    int assistantMessageId;
    QString status;  // "Pending", "Generating", "Paused", "Completed", "Error", "Canceled"
    QString errorText;
    int sortOrder;
    QDateTime createdAt;
};

struct QueueNotification {
    int id;
    QString bookFilepath;
    int messageId;
    QString type;  // "responded", "error"
    QString text;
    bool dismissed;
};

class PromptQueueManager : public QObject {
    Q_OBJECT
   public:
    explicit PromptQueueManager(OllamaClient* ollamaClient, QObject* parent = nullptr);
    ~PromptQueueManager();

    bool initDatabase();
    void loadJobs();

    // Queue operations
    int enqueueJob(const QString& bookFilepath, int userMessageId, int assistantMessageId, const QString& model,
                   const QString& systemPrompt, const QString& promptText);

    void pauseQueue();
    void resumeQueue();
    bool isQueuePaused() const;

    void pauseJob(int jobId);
    void resumeJob(int jobId);
    void cancelJob(int jobId);

    void moveJobUp(int jobId);
    void moveJobDown(int jobId);

    QList<QueueJob> getJobs() const;
    QList<QueueNotification> getUnreadNotifications() const;

    void dismissNotification(int notificationId);
    void addNotification(const QString& bookFilepath, int messageId, const QString& type, const QString& text);

    // Current generation info
    int currentGeneratingJobId() const;

   signals:
    void queueUpdated(int pendingCount, int totalCount);
    void jobStatusChanged(int jobId, const QString& newStatus);
    void chunkReceived(int jobId, const QString& bookFilepath, int assistantMessageId, const QString& chunk,
                       const QString& fullText);
    void jobCompleted(int jobId, const QString& bookFilepath, int assistantMessageId, const QString& fullText);
    void jobError(int jobId, const QString& bookFilepath, int assistantMessageId, const QString& errorText);
    void notificationsUpdated(int unreadCount);

   private slots:
    void processNextJob();
    void onChunkReceived(const QString& chunk);
    void onGenerationComplete(const QString& fullText);
    void onGenerationError(const QString& error);

   private:
    void* m_db;  // sqlite3* handle for queue.db
    OllamaClient* m_ollamaClient;
    QTimer* m_processTimer;

    bool m_isQueuePaused;
    bool m_isGenerating;
    int m_currentJobId;
    QString m_currentFullText;

    // Active book database info
    void* m_activeBookDb;  // sqlite3* handle for active book
    QString m_activeBookFilepath;
    bool openBookDatabase(const QString& filepath);
    void closeBookDatabase();
    bool updateBookMessage(int messageId, const QString& content);

    int getNextSortOrder();
    void updateJobStatus(int jobId, const QString& status, const QString& errorText = QString());
};

#endif  // PROMPTQUEUEMANAGER_H
