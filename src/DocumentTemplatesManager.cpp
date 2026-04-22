#include "DocumentTemplatesManager.h"

#include <QCoreApplication>

QList<DocumentTemplate> DocumentTemplatesManager::getBuiltInTemplates() {
    QList<DocumentTemplate> templates;
    templates.append(
        {"builtin:empty", QCoreApplication::translate("DocumentTemplatesManager", "Empty Document"), "", "built-in"});
    return templates;
}

QList<DocumentTemplate> DocumentTemplatesManager::getGlobalTemplates() {
    QList<DocumentTemplate> templates;
    QSettings settings;
    QVariantList list = settings.value("globalDocumentTemplates").toList();
    for (const QVariant& v : list) {
        QVariantMap map = v.toMap();
        templates.append({map["id"].toString(), map["name"].toString(), map["content"].toString(), "global"});
    }
    return templates;
}

void DocumentTemplatesManager::setGlobalTemplates(const QList<DocumentTemplate>& templates) {
    QSettings settings;
    QVariantList list;
    for (const DocumentTemplate& tpl : templates) {
        QVariantMap map;
        map["id"] = tpl.id;
        map["name"] = tpl.name;
        map["content"] = tpl.content;
        list.append(map);
    }
    settings.setValue("globalDocumentTemplates", list);
}

QList<DocumentTemplate> DocumentTemplatesManager::getDatabaseTemplates(const BookDatabase* db) {
    QList<DocumentTemplate> templates;
    if (!db) return templates;
    QList<DocumentNode> dbTpls = db->getTemplates(-1);
    for (const DocumentNode& doc : dbTpls) {
        templates.append({"db:" + QString::number(doc.id), doc.title, doc.content, "database"});
    }
    return templates;
}

QList<DocumentTemplate> DocumentTemplatesManager::getMergedTemplates(const BookDatabase* db) {
    QList<DocumentTemplate> templates = getBuiltInTemplates();
    templates.append(getGlobalTemplates());
    if (db) {
        templates.append(getDatabaseTemplates(db));
    }
    return templates;
}
