import re

content = open('src/db/Migrations.cpp').read()
content = content.replace(
    '''bool MigrationRunner::getCurrentVersion(Database& db, int& version, QString* error) {
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
}''',
    '''bool MigrationRunner::getCurrentVersion(Database& db, int& version, QString* error) {
    int pragmaVersion = 0;
    if (!db.queryInt("PRAGMA user_version;", pragmaVersion, error)) {
        return false;
    }

    int tableVersion = 0;
    if (!db.queryInt("SELECT MAX(version) FROM schema_version;", tableVersion, error)) {
        // Table might be empty, that's fine if version is 0
        tableVersion = 0;
    }

    if (pragmaVersion > 0 && tableVersion > 0 && pragmaVersion != tableVersion) {
        if (error) *error = QString("Version disagreement: PRAGMA user_version (%1) does not match schema_version table (%2)").arg(pragmaVersion).arg(tableVersion);
        return false;
    }

    version = std::max(pragmaVersion, tableVersion);
    return true;
}'''
)
with open('src/db/Migrations.cpp', 'w') as f:
    f.write(content)
