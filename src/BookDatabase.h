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
    bool isExpanded = false;
};

struct DocumentNode {
    int id;
    int folderId;  // 0 if root
    int parentId;  // 0 if root
    QString title;
    QString content;
    QDateTime timestamp;
    bool isFolder;  // Deprecated, but keeping for compatibility during migration if needed
    QString metadata;
    QString targetType;  // Optional, e.g., 'document', 'note', 'template'
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
    bool isExpanded = false;
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
    QString targetAction;
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
    int targetId;
    QString targetType;  // "document", "message"
    QString type;        // "responded_to", "error"
    bool isDismissed;
    QDateTime timestamp;
};

class BookDatabase {
   public:
    explicit BookDatabase(const QString& filepath);
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
    int addDocument(int folderId, const QString& title, const QString& content, int parentId = 0,
                    const QString& metadata = "");
    bool updateDocument(int id, const QString& newTitle, const QString& newContent, const QString& metadata = "");
    QList<DocumentNode> getDocuments(int folderId = -1) const;  // -1 for all, 0 for root
    std::optional<DocumentNode> getDocument(int id) const;
    bool deleteDocument(int id);
    bool addDocumentHistory(int documentId, const QString& actionType, const QString& content);

    struct DocumentHistoryEntry {
        int id;
        QString actionType;
        QString content;
        QString timestamp;
    };
    QList<DocumentHistoryEntry> getDocumentHistory(int documentId) const;
    int addDocumentHistoryReturningId(int documentId, const QString& actionType, const QString& content);

    // Merges
    struct DocumentMergeEntry {
        int id;
        int documentId;
        QString sourceDocumentIds;  // comma separated or JSON array
        QString prompt;
        QString model;
        QString timestamp;
        int versionHistoryId;
    };
    int addDocumentMerge(int documentId, const QString& sourceDocumentIds, const QString& prompt, const QString& model,
                         int versionHistoryId = 0);
    std::optional<DocumentMergeEntry> getDocumentMerge(int documentId) const;
    bool updateDocumentMergeVersion(int mergeId, int versionHistoryId);

    // Templates
    int addTemplate(int folderId, const QString& title, const QString& content);
    bool updateTemplate(int id, const QString& newTitle, const QString& newContent);
    QList<DocumentNode> getTemplates(int folderId = -1) const;
    bool deleteTemplate(int id);

    // Drafts
    int addDraft(int folderId, const QString& title, const QString& content, int parentId = 0,
                 const QString& targetType = "document");
    bool updateDraft(int id, const QString& newTitle, const QString& newContent);
    QList<DocumentNode> getDrafts(int folderId = -1) const;
    bool deleteDraft(int id);

    // Notes
    int addNote(int folderId, const QString& title, const QString& content);
    bool updateNote(int id, const QString& newTitle, const QString& newContent);
    QList<NoteNode> getNotes(int folderId = -1) const;
    bool deleteNote(int id);

    // Folders
    int addFolder(int parentId, const QString& name, const QString& type, bool isExpanded = false);
    bool updateFolder(int id, const QString& newName);
    bool deleteFolder(int id);
    void setFolderExpanded(int id, bool expanded);
    void setMessageExpanded(int id, bool expanded);
    QList<FolderNode> getFolders(const QString& type) const;
    bool moveItem(const QString& table, int id, int newFolderId);
    bool moveFolder(int id, int newParentId);

    // Queue
    void cleanupDeadProcessingItems();
    int enqueuePrompt(int messageId, const QString& model, const QString& prompt, int priority = 0,
                      const QString& targetType = "message", int parentId = 0, const QString& targetAction = "");
    QList<QueueItem> getQueue() const;

    struct QueueStats {
        int pending = 0;
        int processing = 0;
        int completed = 0;
        int error = 0;
    };
    QueueStats getQueueStats() const;

    bool isGenerating(int targetId, const QString& targetType, const QString& targetAction) const;
    bool updateQueueProcessingId(int id, int processingId);
    bool updateQueueError(int id, const QString& error);
    bool updateQueueItemPrompt(int id, const QString& prompt);
    bool updateQueueItemModel(int id, const QString& model);
    bool updateQueueItemState(int id, const QString& state, const QString& response = "");
    bool deleteQueueItem(int id);

    // Comments
    int addComment(const QString& entityType, int entityId, const QString& content);
    bool updateComment(int id, const QString& newContent);
    QList<CommentNode> getComments(const QString& entityType, int entityId) const;
    bool deleteComment(int id);

    // Notifications
    int addNotification(int targetId, const QString& targetType, const QString& type);
    QList<Notification> getNotifications(bool includeDismissed = false) const;
    bool dismissNotification(int id);
    bool dismissNotificationByTarget(int targetId, const QString& targetType);
    bool dismissNotificationByTargetAndType(int targetId, const QString& targetType, const QString& type);

    QString getDatabaseDebugInfo() const;

   private:
    QString m_filepath;
    void* m_db;  // sqlite3* handle
    bool m_isOpen;
};

#endif  // BOOKDATABASE_H