#ifndef DB_MIGRATIONS_H
#define DB_MIGRATIONS_H

#include <QString>
#include <functional>
#include <vector>

#include "Database.h"

namespace db {

struct Migration {
    int fromVersion = 0;
    int toVersion = 0;
    QString name;
    std::function<bool(Database&)> apply;
};

class MigrationRunner {
   public:
    void addMigration(const Migration& migration);

    // Applies migrations, wrapping each in a transaction.
    // Returns true if success or already up to date, false on error.
    bool run(Database& db, QString* error = nullptr);

   private:
    std::vector<Migration> m_migrations;
    bool initSchemaVersionTable(Database& db, QString* error);
    bool getCurrentVersion(Database& db, int& version, QString* error);
};

}  // namespace db

#endif  // DB_MIGRATIONS_H
