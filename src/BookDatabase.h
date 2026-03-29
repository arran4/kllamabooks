#ifndef BOOKDATABASE_H
#define BOOKDATABASE_H

#include <QDateTime>
#include <QList>
#include <QSet>
#include <QString>

struct MessageNode {
    int id;
    int parentId;  // 0 if root
    int folderId;  // 0 if root (only relevant for root messages)
    QString content;
    QString role;  // "user" or "assistant"
    QDateTime timestamp;
    QList<MessageNode*> children;
};

struct DocumentNode {
    int id;
    int folderId;  // 0 if root
    int parentId;  // 0 if root
    QString title;
    QString content;
    QDateTime timestamp;
    bool isFolder;  // Deprecated, but keeping for compatibility during migration if needed
};

struct ChatNode {
    int id;  // Maps to message_id
    QString title;
    QString systemPrompt;
    QString sendBehavior;
    QString model;
    QString multiLine;
    QString draftPrompt;
    QString userNote;
    int version;
};

struct NoteNode {
    int id;
    int folderId;  // 0 if root
    QString title;
    QString content;
    QDateTime timestamp;
};

struct FolderNode {
    int id;
    int parentId;
    QString name;
    QString type;  // "documents", "notes", "templates", "drafts"
    QDateTime timestamp;
    int position;
};

struct QueueItem {
    int id;
    int messageId;
    QString model;
    QString prompt;
    int processingId;
    QString lastError;
    int priority;
    QDateTime timestamp;
    QString targetType;  // "message" or "document"
    QString state;       // "pending", "processing", "completed", "error"
    QString response;
    int parentId;
};

struct CommentNode {
    int id;
    QString entityType;
    int entityId;
    QString content;
    QDateTime timestamp;
};

struct Notification {
    int id;
    int messageId;
    QString type;  // "responded_to", "error"
    bool isDismissed;
    QDateTime timestamp;
};

class BookDatabase {
   public:
    BookDatabase(const QString& filepath);
    ~BookDatabase();

    bool open(const QString& password);
    void close();
    bool isOpen() const;
    QString filepath() const;

    // Database operations
    bool initSchema();

    // Messages
    int addMessage(int parentId, const QString& content, const QString& role, int folderId = 0);
    bool updateMessage(int id, const QString& newContent);
    bool deleteMessage(int id);
    QList<MessageNode> getMessages() const;
    int getRootMessageId(int messageId) const;
    QString getInheritedSetting(int messageId, const QString& key) const;

    // Chats
    ChatNode getChat(int messageId) const;
    bool updateChat(const ChatNode& chat);
    QSet<int> getAllChatIds() const;

    // Settings
    void setSetting(const QString& scope, int targetId, const QString& key, const QString& value);
    QString getSetting(const QString& scope, int targetId, const QString& key,
                       const QString& defaultValue = QString()) const;

    // Documents
    int addDocument(int folderId, const QString& title, const QString& content, int parentId = 0);
    bool updateDocument(int id, const QString& newTitle, const QString& newContent);
    QList<DocumentNode> getDocuments(int folderId = -1) const;  // -1 for all, 0 for root
    bool deleteDocument(int id);
    bool addDocumentHistory(int documentId, const QString& actionType, const QString& content);

    struct DocumentHistoryEntry {
        int id;
        QString actionType;
        QString content;
        QString timestamp;
    };
    QList<DocumentHistoryEntry> getDocumentHistory(int documentId) const;

    // Templates
    int addTemplate(int folderId, const QString& title, const QString& content);
    bool updateTemplate(int id, const QString& newTitle, const QString& newContent);
    QList<DocumentNode> getTemplates(int folderId = -1) const;

    // Drafts
    int addDraft(int folderId, const QString& title, const QString& content);
    bool updateDraft(int id, const QString& newTitle, const QString& newContent);
    QList<DocumentNode> getDrafts(int folderId = -1) const;
    bool deleteDraft(int id);

    // Notes
    int addNote(int folderId, const QString& title, const QString& content);
    bool updateNote(int id, const QString& newTitle, const QString& newContent);
    QList<NoteNode> getNotes(int folderId = -1) const;
    bool deleteNote(int id);

    // Folders
    int addFolder(int parentId, const QString& name, const QString& type);
    bool updateFolder(int id, const QString& newName);
    bool deleteFolder(int id);
    QList<FolderNode> getFolders(const QString& type) const;
    bool moveItem(const QString& table, int id, int newFolderId);
    bool moveFolder(int id, int newParentId);

    // Queue
    int enqueuePrompt(int messageId, const QString& model, const QString& prompt, int priority = 0,
                      const QString& targetType = "message", int parentId = 0);
    QList<QueueItem> getQueue() const;
    bool updateQueueProcessingId(int id, int processingId);
    bool updateQueueError(int id, const QString& error);
    bool updateQueueItemPrompt(int id, const QString& prompt);
    bool updateQueueItemState(int id, const QString& state, const QString& response = "");
    bool deleteQueueItem(int id);

    // Comments
    int addComment(const QString& entityType, int entityId, const QString& content);
    bool updateComment(int id, const QString& newContent);
    QList<CommentNode> getComments(const QString& entityType, int entityId) const;
    bool deleteComment(int id);

    // Notifications
    int addNotification(int messageId, const QString& type);
    QList<Notification> getNotifications(bool includeDismissed = false) const;
    bool dismissNotification(int id);
    bool dismissNotificationByMessageId(int messageId);

    QString getDatabaseDebugInfo() const;

   private:
    QString m_filepath;
    void* m_db;  // sqlite3* handle
    bool m_isOpen;
};

#endif  // BOOKDATABASE_H