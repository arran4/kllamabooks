#ifndef DOCUMENTTEMPLATESMANAGER_H
#define DOCUMENTTEMPLATESMANAGER_H

#include <QList>
#include <QSettings>
#include <QString>

#include "BookDatabase.h"
#include "TemplateModel.h"

struct DocumentTemplate {
    QString id;
    QString name;
    QString content;
    QString source;  // "built-in", "global", "database", "external-cache"
    TemplateSource sourceKind = TemplateSource::BuiltIn;
    TemplatePurpose purpose = TemplatePurpose::DocumentCreation;
    QString externalUrl;

    bool isReadOnly() const { return isTemplateSourceReadOnly(sourceKind); }
};

class DocumentTemplatesManager {
   public:
    static QList<DocumentTemplate> getBuiltInTemplates();
    static QList<DocumentTemplate> getGlobalTemplates();
    static void setGlobalTemplates(const QList<DocumentTemplate>& templates);
    static QList<DocumentTemplate> getExternalCacheTemplates();
    static void setExternalCacheTemplates(const QList<DocumentTemplate>& templates);

    static QList<DocumentTemplate> getDatabaseTemplates(const BookDatabase* db);

    static QList<DocumentTemplate> getMergedTemplates(const BookDatabase* db);
    static QList<DocumentTemplate> getTemplatesByPurpose(const BookDatabase* db, TemplatePurpose purpose);
};

#endif  // DOCUMENTTEMPLATESMANAGER_H
