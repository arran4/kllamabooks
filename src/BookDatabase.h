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

class BookDatabase {
   public:
    BookDatabase(const QString& filepath);
    ~BookDatabase();

    bool open(const QString& password);
    void close();
    bool isOpen() const;

    // Database operations
    bool initSchema();
    int addMessage(int parentId, const QString& content, const QString& role);
    bool updateMessage(int id, const QString& newContent);
    bool deleteMessage(int id);
    QList<MessageNode> getMessages() const;

   private:
    QString m_filepath;
    void* m_db;  // sqlite3* handle
    bool m_isOpen;
};

#endif  // BOOKDATABASE_H