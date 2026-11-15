#include "BookTreeController.h"

#include <sqlcipher/sqlite3.h>

#include "../db/Database.h"
#include "../store/ChatStore.h"

namespace controllers {

NavigationResolution BookTreeController::resolveNavigation(BookDatabase* db, const store::ItemRef& ref) {
    NavigationResolution res;
    res.ref = ref;
    if (!db || !db->isOpen() || !ref.isValid() || ref.id <= 0) return res;

    sqlite3* raw = reinterpret_cast<sqlite3*>(db->getDatabaseHandleForTesting());
    if (!raw) return res;
    db::Database database(raw);

    switch (ref.kind) {
        case store::ItemKind::Document:
            res.view = NavigationTargetView::DocumentEditor;
            res.resolvedNodeId = ref.id;
            break;
        case store::ItemKind::Note:
            res.view = NavigationTargetView::NoteEditor;
            res.resolvedNodeId = ref.id;
            break;
        case store::ItemKind::Template:
            res.view = NavigationTargetView::TemplateView;
            res.resolvedNodeId = ref.id;
            break;
        case store::ItemKind::Draft:
            res.view = NavigationTargetView::DraftView;
            res.resolvedNodeId = ref.id;
            break;
        case store::ItemKind::ChatSession: {
            res.view = NavigationTargetView::ChatSession;
            store::ChatStore chatStore(database);
            if (auto chatOpt = chatStore.getChat(ref.id)) {
                res.resolvedNodeId = chatOpt->rootMessageId;
            } else {
                res.resolvedNodeId = ref.id;
            }
            break;
        }
        case store::ItemKind::ChatNode:
            res.view = NavigationTargetView::ChatSession;
            res.resolvedNodeId = ref.id;
            break;
        case store::ItemKind::Folder:
        case store::ItemKind::Unknown:
            break;
    }
    return res;
}

QList<store::SearchHit> BookTreeController::searchBook(BookDatabase* db, const QString& query,
                                                       std::optional<store::ItemKind> kindFilter, int limit) {
    if (!db || !db->isOpen()) return {};
    sqlite3* raw = reinterpret_cast<sqlite3*>(db->getDatabaseHandleForTesting());
    if (!raw) return {};
    db::Database database(raw);
    store::SearchStore searchStore(database);
    return searchStore.search(query, kindFilter, limit);
}

}  // namespace controllers
