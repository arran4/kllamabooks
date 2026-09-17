#include "Migrations.h"

#include <algorithm>

#include "Transaction.h"

namespace db {

void MigrationRunner::addMigration(const Migration& migration) { m_migrations.push_back(migration); }

bool MigrationRunner::run(Database& db, QString* error) {
    std::sort(m_migrations.begin(), m_migrations.end(),
              [](const Migration& a, const Migration& b) { return a.fromVersion < b.fromVersion; });

    int currentVersion = 0;
    syncLegacyPragmaVersion(db, currentVersion, error);

    if (!initSchemaVersionTable(db, error)) {
        return false;
    }

    getCurrentVersion(db, currentVersion, error);

    for (const auto& migration : m_migrations) {
        if (currentVersion >= migration.toVersion) {
            continue;
        }

        if (currentVersion != migration.fromVersion) {
            if (error)
                *error = QString("Migration gap: expected version %1 but db is at %2")
                             .arg(migration.fromVersion)
                             .arg(currentVersion);
            return false;
        }

        Transaction tx(db);
        if (tx.isFailed()) {
            if (error) *error = "Failed to start transaction for migration";
            return false;
        }

        if (!migration.apply(db)) {
            if (error) *error = QString("Failed to apply migration: %1").arg(migration.name);
            return false;
        }

        QString updateSql =
            QString("INSERT OR REPLACE INTO schema_version (version) VALUES (%1);").arg(migration.toVersion);
        if (!db.execute(updateSql, error)) {
            return false;
        }

        QString pragmaSql = QString("PRAGMA user_version = %1;").arg(migration.toVersion);
        if (!db.execute(pragmaSql, error)) {
            return false;
        }

        if (!tx.commit()) {
            if (error) *error = QString("Failed to commit migration: %1").arg(migration.name);
            return false;
        }

        currentVersion = migration.toVersion;
    }

    return true;
}

bool MigrationRunner::syncLegacyPragmaVersion(Database& db, int& version, QString* error) {
    if (!db.queryInt("PRAGMA user_version;", version, error)) {
        version = 0;
    }
    return true;
}

bool MigrationRunner::initSchemaVersionTable(Database& db, QString* error) {
    int count = 0;
    if (!db.queryInt("SELECT count(name) FROM sqlite_master WHERE type='table' AND name='schema_version';", count,
                     error)) {
        return false;
    }

    if (count == 0) {
        if (!db.execute("CREATE TABLE IF NOT EXISTS schema_version (version INTEGER PRIMARY KEY, applied_at DATETIME "
                        "DEFAULT CURRENT_TIMESTAMP);",
                        error)) {
            return false;
        }

        int userVersion = 0;
        if (db.queryInt("PRAGMA user_version;", userVersion) && userVersion > 0) {
            QString syncSql = QString("INSERT OR REPLACE INTO schema_version (version) VALUES (%1);").arg(userVersion);
            if (!db.execute(syncSql, error)) {
                return false;
            }
        }
    }
    return true;
}

bool MigrationRunner::getCurrentVersion(Database& db, int& version, QString* error) {
    int pragmaVersion = 0;
    if (db.queryInt("PRAGMA user_version;", pragmaVersion)) {
        version = pragmaVersion;
    }

    int tableVersion = 0;
    bool hasTableVersion = db.queryInt("SELECT MAX(version) FROM schema_version;", tableVersion);

    if (hasTableVersion && tableVersion > version) {
        version = tableVersion;
    }

    return true;
}

}  // namespace db
