#ifndef AIOPERATIONSMANAGER_H
#define AIOPERATIONSMANAGER_H

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QSettings>
#include <QString>

#include "BookDatabase.h"

struct AIOperation {
    QString id;
    QString name;
    QString prompt;
    QString source;     // "built-in", "global", "database"
    QString inputType;  // "single", "multiple", "any"
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
