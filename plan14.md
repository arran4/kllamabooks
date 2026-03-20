Wait! "11. The breadcrumbs stop working and get stuck on an item, they should update as I look at things"
Breadcrumbs are managed by `updateBreadcrumbs()`. It is called in `updateLinearChatView`.
Is it called anywhere else?
If I open a document or note, does it update breadcrumbs?
Let's check `onOpenBooksSelectionChanged`!
When you select a document or note, it shows `docContainer` or `noteContainer`. Does it update breadcrumbs?
No! It doesn't call `updateBreadcrumbs()`!
In `onOpenBooksSelectionChanged`:
```cpp
    } else {
        int nodeId = item->data(Qt::UserRole).toInt();
        if (type == "document" || type == "template" || type == "draft") {
            // ...
            mainContentStack->setCurrentWidget(docContainer);
        } else if (type == "note") {
            // ...
            mainContentStack->setCurrentWidget(noteContainer);
        } else {
            // chat... calls updateLinearChatView -> which calls updateBreadcrumbs!
        }
    }
```
If we add `updateBreadcrumbs();` at the end of `onOpenBooksSelectionChanged`, it will update!
But wait, what if `type == "book"` or `type == "chats_folder"`?
Then it shows `vfsExplorer`. Does it update breadcrumbs?
If it shows `vfsExplorer`, it does NOT call `updateBreadcrumbs()`!
We just need to call `updateBreadcrumbs()` at the end of `onOpenBooksSelectionChanged` so it ALWAYS updates to the selected item!
Let's see: `updateBreadcrumbs()` uses `openBooksTree->currentIndex()` to get the current item! So it will work perfectly!
Wait! What if we select a file inside `vfsExplorer`? We already double click to select it in `openBooksTree`, which triggers `onOpenBooksSelectionChanged`, which then updates the breadcrumbs!
So adding `updateBreadcrumbs()` to the end of `onOpenBooksSelectionChanged` fixes issue 11!
