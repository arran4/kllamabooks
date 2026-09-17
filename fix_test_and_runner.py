import re

content = open('src/db/Migrations.cpp').read()
content = content.replace(
    'bool MigrationRunner::getCurrentVersion(Database& db, int& version, QString* error) {\n    int pragmaVersion = 0;\n    if (db.queryInt("PRAGMA user_version;", pragmaVersion)) {\n        version = pragmaVersion;\n    }\n\n    int tableVersion = 0;\n    bool hasTableVersion = db.queryInt("SELECT MAX(version) FROM schema_version;", tableVersion);\n\n    if (hasTableVersion && tableVersion > version) {\n        version = tableVersion;\n    }\n\n    return true;\n}',
    '''bool MigrationRunner::getCurrentVersion(Database& db, int& version, QString* error) {
    int pragmaVersion = 0;
    db.queryInt("PRAGMA user_version;", pragmaVersion);

    int tableVersion = 0;
    bool hasTableVersion = db.queryInt("SELECT MAX(version) FROM schema_version;", tableVersion);

    if (hasTableVersion && pragmaVersion > 0 && tableVersion > 0 && pragmaVersion != tableVersion) {
        if (error) *error = QString("Version disagreement: PRAGMA user_version (%1) does not match schema_version table (%2)").arg(pragmaVersion).arg(tableVersion);
        return false;
    }

    version = std::max(pragmaVersion, tableVersion);
    return true;
}'''
)
content = content.replace('    getCurrentVersion(db, currentVersion, error);', '    if (!getCurrentVersion(db, currentVersion, error)) return false;')

with open('src/db/Migrations.cpp', 'w') as f:
    f.write(content)
