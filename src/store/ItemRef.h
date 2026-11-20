#pragma once

#include <QStandardItem>
#include <QString>
#include <optional>

#include "ArtifactStore.h"

namespace store {

enum class ItemKind {
    Unknown = 0,
    Book,
    ChatsRoot,
    DocsRoot,
    NotesRoot,
    TemplatesRoot,
    DraftsRoot,
    UnifiedRoot,
    Folder,
    ChatSession,
    ChatNode,
    Document,
    Note,
    Template,
    Draft
};

struct ItemRef {
    ItemKind kind = ItemKind::Unknown;
    int id = 0;
    QString rootType;

    bool isValid() const { return kind != ItemKind::Unknown; }
    bool operator==(const ItemRef& other) const {
        return kind == other.kind && id == other.id && rootType == other.rootType;
    }
    bool operator!=(const ItemRef& other) const { return !(*this == other); }
};

inline constexpr int kItemRefKindRole = Qt::UserRole + 20;
inline constexpr int kItemRefRootTypeRole = Qt::UserRole + 21;

inline QString normalizeFolderRootType(const QString& rawType) {
    const QString t = rawType.trimmed().toLower();
    if (t == "document" || t == "documents" || t == "docs_folder" || t == "doc_folder" || t == "folder" ||
        t.isEmpty()) {
        return QStringLiteral("documents");
    }
    if (t == "note" || t == "notes" || t == "notes_folder") {
        return QStringLiteral("notes");
    }
    if (t == "template" || t == "templates" || t == "templates_folder") {
        return QStringLiteral("templates");
    }
    if (t == "draft" || t == "drafts" || t == "drafts_folder") {
        return QStringLiteral("drafts");
    }
    if (t == "chat" || t == "chats" || t == "chats_folder" || t == "chat_session" || t == "chat_node" ||
        t == "messages") {
        return QStringLiteral("chats");
    }
    if (t == "unified" || t == "unified_folder") {
        return QStringLiteral("unified");
    }
    return t;
}

inline QString folderRootToLegacyTreeType(const QString& rootType) {
    const QString norm = normalizeFolderRootType(rootType);
    if (norm == "documents") return QStringLiteral("docs_folder");
    if (norm == "notes") return QStringLiteral("notes_folder");
    if (norm == "templates") return QStringLiteral("templates_folder");
    if (norm == "drafts") return QStringLiteral("drafts_folder");
    if (norm == "chats") return QStringLiteral("chats_folder");
    if (norm == "unified") return QStringLiteral("unified_folder");
    return norm + QStringLiteral("_folder");
}

inline QString itemKindToLegacyTypeString(ItemKind kind, const QString& rootType = QString()) {
    switch (kind) {
        case ItemKind::Book:
            return QStringLiteral("book");
        case ItemKind::ChatsRoot:
            return QStringLiteral("chats_folder");
        case ItemKind::DocsRoot:
            return QStringLiteral("docs_folder");
        case ItemKind::NotesRoot:
            return QStringLiteral("notes_folder");
        case ItemKind::TemplatesRoot:
            return QStringLiteral("templates_folder");
        case ItemKind::DraftsRoot:
            return QStringLiteral("drafts_folder");
        case ItemKind::UnifiedRoot:
            return QStringLiteral("unified_folder");
        case ItemKind::Folder:
            return folderRootToLegacyTreeType(rootType);
        case ItemKind::ChatSession:
            return QStringLiteral("chat_session");
        case ItemKind::ChatNode:
            return QStringLiteral("chat_node");
        case ItemKind::Document:
            return QStringLiteral("document");
        case ItemKind::Note:
            return QStringLiteral("note");
        case ItemKind::Template:
            return QStringLiteral("template");
        case ItemKind::Draft:
            return QStringLiteral("draft");
        case ItemKind::Unknown:
        default:
            return QString();
    }
}

inline ItemKind itemKindFromLegacyTypeString(const QString& type, int id = 0, QString* outRootType = nullptr) {
    if (type == "book") return ItemKind::Book;
    if (type == "chats_folder") {
        if (outRootType) *outRootType = QStringLiteral("chats");
        return id == 0 ? ItemKind::ChatsRoot : ItemKind::Folder;
    }
    if (type == "docs_folder" || type == "doc_folder") {
        if (outRootType) *outRootType = QStringLiteral("documents");
        return id == 0 ? ItemKind::DocsRoot : ItemKind::Folder;
    }
    if (type == "notes_folder") {
        if (outRootType) *outRootType = QStringLiteral("notes");
        return id == 0 ? ItemKind::NotesRoot : ItemKind::Folder;
    }
    if (type == "templates_folder") {
        if (outRootType) *outRootType = QStringLiteral("templates");
        return id == 0 ? ItemKind::TemplatesRoot : ItemKind::Folder;
    }
    if (type == "drafts_folder") {
        if (outRootType) *outRootType = QStringLiteral("drafts");
        return id == 0 ? ItemKind::DraftsRoot : ItemKind::Folder;
    }
    if (type == "unified_folder") {
        if (outRootType) *outRootType = QStringLiteral("unified");
        return id == 0 ? ItemKind::UnifiedRoot : ItemKind::Folder;
    }
    if (type.endsWith("_folder")) {
        if (outRootType) *outRootType = normalizeFolderRootType(type);
        return ItemKind::Folder;
    }
    if (type == "chat_session") {
        if (outRootType) *outRootType = QStringLiteral("chats");
        return ItemKind::ChatSession;
    }
    if (type == "chat_node" || type == "message" || type == "messages") {
        if (outRootType) *outRootType = QStringLiteral("chats");
        return ItemKind::ChatNode;
    }
    if (type == "document" || type == "documents") {
        if (outRootType) *outRootType = QStringLiteral("documents");
        return ItemKind::Document;
    }
    if (type == "note" || type == "notes") {
        if (outRootType) *outRootType = QStringLiteral("notes");
        return ItemKind::Note;
    }
    if (type == "template" || type == "templates") {
        if (outRootType) *outRootType = QStringLiteral("templates");
        return ItemKind::Template;
    }
    if (type == "draft" || type == "drafts") {
        if (outRootType) *outRootType = QStringLiteral("drafts");
        return ItemKind::Draft;
    }
    return ItemKind::Unknown;
}

inline bool isFolderLikeKind(ItemKind kind) {
    return kind == ItemKind::Folder || kind == ItemKind::ChatsRoot || kind == ItemKind::DocsRoot ||
           kind == ItemKind::NotesRoot || kind == ItemKind::TemplatesRoot || kind == ItemKind::DraftsRoot ||
           kind == ItemKind::UnifiedRoot;
}

inline std::optional<ArtifactKind> toArtifactKind(ItemKind kind) {
    switch (kind) {
        case ItemKind::Document:
            return ArtifactKind::Document;
        case ItemKind::Note:
            return ArtifactKind::Note;
        case ItemKind::Template:
            return ArtifactKind::Template;
        case ItemKind::Draft:
            return ArtifactKind::Draft;
        default:
            return std::nullopt;
    }
}

inline void applyItemRef(QStandardItem* item, const ItemRef& ref) {
    if (!item) return;
    item->setData(ref.id, Qt::UserRole);
    item->setData(itemKindToLegacyTypeString(ref.kind, ref.rootType), Qt::UserRole + 1);
    item->setData(static_cast<int>(ref.kind), kItemRefKindRole);
    item->setData(ref.rootType, kItemRefRootTypeRole);
}

inline ItemRef itemRefFromStandardItem(const QStandardItem* item) {
    if (!item) return {};
    const int id = item->data(Qt::UserRole).toInt();
    const QVariant kindVar = item->data(kItemRefKindRole);
    if (kindVar.isValid() && kindVar.toInt() != 0) {
        ItemRef ref;
        ref.kind = static_cast<ItemKind>(kindVar.toInt());
        ref.id = id;
        ref.rootType = item->data(kItemRefRootTypeRole).toString();
        if (ref.rootType.isEmpty()) {
            itemKindFromLegacyTypeString(item->data(Qt::UserRole + 1).toString(), id, &ref.rootType);
        }
        return ref;
    }
    QString rootType;
    const QString legacyType = item->data(Qt::UserRole + 1).toString();
    const ItemKind kind = itemKindFromLegacyTypeString(legacyType, id, &rootType);
    return {kind, id, rootType};
}

}  // namespace store
