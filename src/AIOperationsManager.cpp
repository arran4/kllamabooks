#include "AIOperationsManager.h"

QList<AIOperation> AIOperationsManager::getBuiltInOperations() {
    QList<AIOperation> ops;
    ops.append({"complete", "Complete this text",
                "Complete the following text naturally. Only output the continuation.\n\nText:\n{context}",
                "built-in"});
    ops.append({"replace", "Replace entirely",
                "Rewrite the following document according to your instructions. Only output the rewritten document, "
                "nothing else.\n\nInstructions: {textarea}\n\nDocument:\n{context}",
                "built-in"});
    ops.append({"replace_in_place", "Replace in place",
                "Rewrite the following text according to your instructions. Only output the rewritten text, nothing "
                "else.\n\nInstructions: {textarea}\n\nText:\n{context}",
                "built-in"});
    return ops;
}

QList<AIOperation> AIOperationsManager::getGlobalOperations() {
    QList<AIOperation> ops;
    QSettings settings;
    QVariantList list = settings.value("globalAIOperations").toList();
    for (const QVariant& v : list) {
        QVariantMap map = v.toMap();
        ops.append({map["id"].toString(), map["name"].toString(), map["prompt"].toString(), "global"});
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
        list.append(map);
    }
    settings.setValue("globalAIOperations", list);
}

QList<AIOperation> AIOperationsManager::getDatabaseOperations(BookDatabase* db) {
    QList<AIOperation> ops;
    if (!db) return ops;
    QString jsonStr = db->getSetting("book", 0, "databaseAIOperations", "[]");
    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
    QJsonArray arr = doc.array();
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject obj = arr[i].toObject();
        ops.append({obj["id"].toString(), obj["name"].toString(), obj["prompt"].toString(), "database"});
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
        arr.append(obj);
    }
    QJsonDocument doc(arr);
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
