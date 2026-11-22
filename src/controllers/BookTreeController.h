#pragma once

#include <QString>
#include <optional>

#include "../BookDatabase.h"
#include "../store/ItemRef.h"
#include "../store/SearchStore.h"

namespace controllers {

enum class NavigationTargetView { None, ChatSession, DocumentEditor, NoteEditor, TemplateView, DraftView };

struct NavigationResolution {
    NavigationTargetView view = NavigationTargetView::None;
    store::ItemRef ref;
    int resolvedNodeId = 0;
};

/**
 * @brief Focused controller extracted from MainWindow for typed ItemRef
 * navigation and encrypted book-wide search dispatch.
 */
class BookTreeController {
   public:
    static NavigationResolution resolveNavigation(BookDatabase* db, const store::ItemRef& ref);
    static QList<store::SearchHit> searchBook(BookDatabase* db, const QString& query,
                                              std::optional<store::ItemKind> kindFilter = std::nullopt, int limit = 50);
};

}  // namespace controllers
