#ifndef DOCUMENTTEMPLATESMANAGER_H
#define DOCUMENTTEMPLATESMANAGER_H

#include <QList>
#include <QSettings>
#include <QString>

#include "BookDatabase.h"

struct DocumentTemplate {
    QString id;
    QString name;
    QString content;
    QString source;  // "built-in", "global", "database"
};

class DocumentTemplatesManager {
   public:
    static QList<DocumentTemplate> getBuiltInTemplates();
    static QList<DocumentTemplate> getGlobalTemplates();
    static void setGlobalTemplates(const QList<DocumentTemplate>& templates);

    static QList<DocumentTemplate> getDatabaseTemplates(const BookDatabase* db);

    static QList<DocumentTemplate> getMergedTemplates(const BookDatabase* db);
};

#endif  // DOCUMENTTEMPLATESMANAGER_H
