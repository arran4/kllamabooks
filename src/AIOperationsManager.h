#ifndef AIOPERATIONSMANAGER_H
#define AIOPERATIONSMANAGER_H

#include <QString>
#include <QList>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include "BookDatabase.h"

struct AIOperation {
    QString id;
    QString name;
    QString prompt;
    QString source; // "built-in", "global", "database"
};

class AIOperationsManager {
public:
    static QList<AIOperation> getBuiltInOperations();
    static QList<AIOperation> getGlobalOperations();
    static void setGlobalOperations(const QList<AIOperation>& ops);

    static QList<AIOperation> getDatabaseOperations(BookDatabase* db);
    static void setDatabaseOperations(BookDatabase* db, const QList<AIOperation>& ops);

    static QList<AIOperation> getMergedOperations(BookDatabase* db);
};

#endif
