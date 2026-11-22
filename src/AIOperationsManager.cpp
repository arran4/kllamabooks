#include "AIOperationsManager.h"

namespace {

TemplatePurpose purposeFromInputType(const QString& inputType, const QString& explicitPurpose = QString()) {
    if (!explicitPurpose.isEmpty()) {
        return templatePurposeFromString(explicitPurpose);
    }
    if (inputType == QLatin1String("multiple")) {
        return TemplatePurpose::MultiDocMerge;
    }
    if (inputType == QLatin1String("chat")) {
        return TemplatePurpose::ChatSystemPrompt;
    }
    return TemplatePurpose::SingleDocTransform;
}

AIOperation makeOperation(const QString& id, const QString& name, const QString& prompt, TemplateSource sourceKind,
                          const QString& inputType, const QString& explicitPurpose = QString()) {
    AIOperation op;
    op.id = id;
    op.name = name;
    op.prompt = prompt;
    op.source = templateSourceToString(sourceKind);
    op.inputType = inputType;
    op.sourceKind = sourceKind;
    op.purpose = purposeFromInputType(inputType, explicitPurpose);
    return op;
}

}  // namespace

QList<AIOperation> AIOperationsManager::getBuiltInOperations() {
    QList<AIOperation> ops;
    ops.append(makeOperation("complete", "Complete this text",
                             "Complete the following text naturally. Only output the continuation.\n\nText:\n{context}",
                             TemplateSource::BuiltIn, "single"));
    ops.append(makeOperation("replace", "Replace entirely",
                             "Rewrite the following document according to your instructions. Only output the rewritten "
                             "document, nothing else.\n\nInstructions: {textarea}\n\nDocument:\n{context}",
                             TemplateSource::BuiltIn, "single"));
    ops.append(makeOperation("replace_in_place", "Replace in place",
                             "Rewrite the following text according to your instructions. Only output the rewritten "
                             "text, nothing else.\n\nInstructions: {textarea}\n\nText:\n{context}",
                             TemplateSource::BuiltIn, "single"));
    ops.append(makeOperation("merge", "Merge Documents",
                             "Merge the following documents into one coherent document:\n\n{foreach "
                             "contexts}\nDocument:\n{context}\n{between}\n---\n{end}",
                             TemplateSource::BuiltIn, "multiple"));
    return ops;
}

QList<AIOperation> AIOperationsManager::getGlobalOperations() {
    QList<AIOperation> ops;
    QSettings settings;
    const QVariantList list = settings.value("globalAIOperations").toList();
    for (const QVariant& v : list) {
        const QVariantMap map = v.toMap();
        const QString inputType = map.contains("inputType") ? map["inputType"].toString() : "single";
        const QString explicitPurpose = map.contains("purpose") ? map["purpose"].toString() : QString();
        ops.append(makeOperation(map["id"].toString(), map["name"].toString(), map["prompt"].toString(),
                                 TemplateSource::Global, inputType, explicitPurpose));
    }
    return ops;
}

void AIOperationsManager::setGlobalOperations(const QList<AIOperation>& ops) {
    QSettings settings;
    QVariantList list;
    for (const AIOperation& op : ops) {
        QVariantMap map;
        map["id"] = op.id;
        map["name"] = op.name;
        map["prompt"] = op.prompt;
        map["inputType"] = op.inputType;
        map["purpose"] = templatePurposeToString(op.purpose);
        list.append(map);
    }
    settings.setValue("globalAIOperations", list);
}

QList<AIOperation> AIOperationsManager::getDatabaseOperations(BookDatabase* db) {
    QList<AIOperation> ops;
    if (!db) return ops;
    const QString jsonStr = db->getSetting("book", 0, "databaseAIOperations", "[]");
    const QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
    const QJsonArray arr = doc.array();
    for (int i = 0; i < arr.size(); ++i) {
        const QJsonObject obj = arr[i].toObject();
        const QString inputType = obj.contains("inputType") ? obj["inputType"].toString() : "single";
        const QString explicitPurpose = obj.contains("purpose") ? obj["purpose"].toString() : QString();
        ops.append(makeOperation(obj["id"].toString(), obj["name"].toString(), obj["prompt"].toString(),
                                 TemplateSource::Book, inputType, explicitPurpose));
    }
    return ops;
}

void AIOperationsManager::setDatabaseOperations(BookDatabase* db, const QList<AIOperation>& ops) {
    if (!db) return;
    QJsonArray arr;
    for (const AIOperation& op : ops) {
        QJsonObject obj;
        obj["id"] = op.id;
        obj["name"] = op.name;
        obj["prompt"] = op.prompt;
        obj["inputType"] = op.inputType;
        obj["purpose"] = templatePurposeToString(op.purpose);
        arr.append(obj);
    }
    const QJsonDocument doc(arr);
    db->setSetting("book", 0, "databaseAIOperations", QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
}

QList<AIOperation> AIOperationsManager::getMergedOperations(BookDatabase* db) {
    QList<AIOperation> ops = getBuiltInOperations();
    ops.append(getGlobalOperations());
    if (db) {
        ops.append(getDatabaseOperations(db));
    }
    return ops;
}

QList<AIOperation> AIOperationsManager::getOperationsByPurpose(BookDatabase* db, TemplatePurpose purpose) {
    QList<AIOperation> filtered;
    const QList<AIOperation> all = getMergedOperations(db);
    for (const AIOperation& op : all) {
        if (op.purpose == purpose) {
            filtered.append(op);
        }
    }
    return filtered;
}
