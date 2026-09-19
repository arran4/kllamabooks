#ifndef DB_DATABASE_H
#define DB_DATABASE_H

#include <QString>

struct sqlite3;

namespace db {

class Database {
   public:
    explicit Database(sqlite3* handle);

    sqlite3* handle() const { return m_db; }

    bool execute(const QString& sql, QString* error = nullptr);

    bool queryInt(const QString& sql, int& result, QString* error = nullptr);
    bool hasColumn(const QString& table, const QString& column, bool& exists, QString* error = nullptr);

   private:
    sqlite3* m_db;
};

}  // namespace db

#endif  // DB_DATABASE_H
