#include "DocumentTemplatesManager.h"

#include <QCoreApplication>

namespace {

DocumentTemplate makeTemplate(const QString& id, const QString& name, const QString& content, TemplateSource sourceKind,
                              TemplatePurpose purpose = TemplatePurpose::DocumentCreation,
                              const QString& externalUrl = QString()) {
    DocumentTemplate tpl;
    tpl.id = id;
    tpl.name = name;
    tpl.content = content;
    tpl.source = templateSourceToString(sourceKind);
    tpl.sourceKind = sourceKind;
    tpl.purpose = purpose;
    tpl.externalUrl = externalUrl;
    return tpl;
}

}  // namespace

QList<DocumentTemplate> DocumentTemplatesManager::getBuiltInTemplates() {
    QList<DocumentTemplate> templates;
    templates.append(makeTemplate(QStringLiteral("builtin:empty"),
                                  QCoreApplication::translate("DocumentTemplatesManager", "Empty Document"), QString(),
                                  TemplateSource::BuiltIn));
    return templates;
}

QList<DocumentTemplate> DocumentTemplatesManager::getGlobalTemplates() {
    QList<DocumentTemplate> templates;
    QSettings settings;
    const QVariantList list = settings.value("globalDocumentTemplates").toList();
    for (const QVariant& v : list) {
        const QVariantMap map = v.toMap();
        const TemplatePurpose purpose = map.contains("purpose") ? templatePurposeFromString(map["purpose"].toString())
                                                                : TemplatePurpose::DocumentCreation;
        templates.append(makeTemplate(map["id"].toString(), map["name"].toString(), map["content"].toString(),
                                      TemplateSource::Global, purpose));
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
        map["purpose"] = templatePurposeToString(tpl.purpose);
        list.append(map);
    }
    settings.setValue("globalDocumentTemplates", list);
}

QList<DocumentTemplate> DocumentTemplatesManager::getExternalCacheTemplates() {
    QList<DocumentTemplate> templates;
    QSettings settings;
    const QVariantList list = settings.value("externalCacheDocumentTemplates").toList();
    for (const QVariant& v : list) {
        const QVariantMap map = v.toMap();
        const TemplatePurpose purpose = map.contains("purpose") ? templatePurposeFromString(map["purpose"].toString())
                                                                : TemplatePurpose::DocumentCreation;
        templates.append(makeTemplate(map["id"].toString(), map["name"].toString(), map["content"].toString(),
                                      TemplateSource::ExternalCache, purpose, map["externalUrl"].toString()));
    }
    return templates;
}

void DocumentTemplatesManager::setExternalCacheTemplates(const QList<DocumentTemplate>& templates) {
    QSettings settings;
    QVariantList list;
    for (const DocumentTemplate& tpl : templates) {
        QVariantMap map;
        map["id"] = tpl.id;
        map["name"] = tpl.name;
        map["content"] = tpl.content;
        map["purpose"] = templatePurposeToString(tpl.purpose);
        map["externalUrl"] = tpl.externalUrl;
        list.append(map);
    }
    settings.setValue("externalCacheDocumentTemplates", list);
}

QList<DocumentTemplate> DocumentTemplatesManager::getDatabaseTemplates(const BookDatabase* db) {
    QList<DocumentTemplate> templates;
    if (!db) return templates;
    const QList<DocumentNode> dbTpls = db->getTemplates(-1);
    for (const DocumentNode& doc : dbTpls) {
        templates.append(makeTemplate("db:" + QString::number(doc.id), doc.title, doc.content, TemplateSource::Book));
    }
    return templates;
}

QList<DocumentTemplate> DocumentTemplatesManager::getMergedTemplates(const BookDatabase* db) {
    QList<DocumentTemplate> templates = getBuiltInTemplates();
    templates.append(getGlobalTemplates());
    templates.append(getExternalCacheTemplates());
    if (db) {
        templates.append(getDatabaseTemplates(db));
    }
    return templates;
}

QList<DocumentTemplate> DocumentTemplatesManager::getTemplatesByPurpose(const BookDatabase* db,
                                                                        TemplatePurpose purpose) {
    QList<DocumentTemplate> filtered;
    const QList<DocumentTemplate> all = getMergedTemplates(db);
    for (const DocumentTemplate& tpl : all) {
        if (tpl.purpose == purpose) {
            filtered.append(tpl);
        }
    }
    return filtered;
}
