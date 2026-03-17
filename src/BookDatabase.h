#ifndef BOOKDATABASE_H
#define BOOKDATABASE_H

#include <QDateTime>
#include <QList>
#include <QString>

struct MessageNode {
    int id;
    int parentId;  // 0 if root
    QString content;
    QString role;  // "user" or "assistant"
    QDateTime timestamp;
    QList<MessageNode*> children;
};

struct DocumentNode {
    int id;
    int parentId;
    QString title;
    QString content;
    QDateTime timestamp;
};

struct NoteNode {
    int id;
    QString title;
    QString content;
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
    int addMessage(int parentId, const QString& content, const QString& role);
    bool updateMessage(int id, const QString& newContent);
    bool deleteMessage(int id);
    QList<MessageNode> getMessages() const;

    // Settings
    void setSetting(const QString& scope, int targetId, const QString& key, const QString& value);
    QString getSetting(const QString& scope, int targetId, const QString& key, const QString& defaultValue = QString()) const;

    // Documents
    int addDocument(int parentId, const QString& title, const QString& content);
    bool updateDocument(int id, const QString& newTitle, const QString& newContent);
    QList<DocumentNode> getDocuments() const;

    // Notes
    int addNote(const QString& title, const QString& content);
    bool updateNote(int id, const QString& newTitle, const QString& newContent);
    QList<NoteNode> getNotes() const;

private:
    QString m_filepath;
    void* m_db;  // sqlite3* handle
    bool m_isOpen;
};

#endif  // BOOKDATABASE_H