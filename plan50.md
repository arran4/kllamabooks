Wait, `DragMove` returns `true` ALWAYS!
Is it possible that if you return `true` from `DragMove`, `QTreeView` doesn't know WHAT action you accepted?
If I `acceptProposedAction()`, it tells Qt to allow the drop.
Wait, if you completely block `DragMove` by returning `true`, does `QTreeView` stop updating its internal drop indicator?
YES! If `eventFilter` consumes the `QEvent::DragMove`, the `QTreeView` NEVER gets the `DragMove` event!
If the `QTreeView` NEVER gets the `DragMove` event, IT CANNOT DRAW THE DROP INDICATOR!
If there is no drop indicator, maybe the user thinks "it doesn't do anything"?
Wait! If the `QTreeView` never gets the `DragMove` event, it NEVER sets its internal `dropIndex`!
BUT `dropEvent->position()` STILL works!
Wait, but if `eventFilter` consumes `DragEnter` and `DragMove`, it completely bypasses Qt's visual drag-and-drop feedback!
But the user said "tree view drag drop works." IN THE FIRST COMMENT!
Before I added `openBooksTree->viewport()->installEventFilter(this)`!
Before I added that, the native `QTreeView` handled `DragEnter` and `DragMove`! AND it drew the drop indicator!
AND when I dropped, the native `QTreeView` handled the drop! (Because my code didn't intercept the viewport).
NOW that my code intercepts the viewport, my code EATS `DragEnter` and `DragMove` and `Drop`!
If it eats `DragMove`, the drop indicator IS GONE!
If the drop indicator is gone, the user says "it doesn't do anything" because they don't even see the drop target highlight!
Ah!!!
If I intercept `DragEnter` and `DragMove` on the viewport, I shouldn't block them if I want native visual feedback!
Wait, if I let them pass to native, does the native `QTreeView` allow dropping?
Yes! `QTreeView` allows dropping!
So I SHOULD NOT return `true` for `DragEnter` and `DragMove`! I should just `return KXmlGuiWindow::eventFilter(obj, event)`!
Wait! If I return `false` for `DragEnter` and `DragMove`, the `QTreeView` will handle them natively and draw the drop indicator!
And THEN when the user drops, `Drop` event fires.
If I intercept `Drop`, I DO `return true` to block the native drop logic (which moves the item), and INSTED I do the DB move and manual `takeRow`/`appendRow`!
YES!!!
This explains EVERYTHING!
Because I intercepted `DragMove` and returned `true`, the `QTreeView` stopped drawing the drop indicator!
So the user said "it doesn't do anything" because it looks like dragging is broken!

Wait, but what if they drop it and it ACTUALLY doesn't do anything?
If I let `QTreeView` handle `DragMove`, it might `ignore()` the event if the target is invalid!
If it `ignore()`s the event natively, it might not even emit a `Drop` event!
But `QTreeView` natively accepts drops onto folders!
So it WILL emit the `Drop` event!

Let's remove my `DragMove` and `DragEnter` interceptions for `openBooksTree` and `vfsExplorer`!
Wait, if I remove them, will `QListView` (VFS Explorer) natively accept drops?
Yes, `vfsExplorer->setDragDropMode(QAbstractItemView::DragDrop)` means it accepts drops natively!

Wait, I only need to intercept `QEvent::Drop`!
Is this true?
In `eventFilter`, if I just let `DragEnter` and `DragMove` pass through:
```cpp
    if (event->type() == QEvent::DragEnter) {
        // ...
        return KXmlGuiWindow::eventFilter(obj, event);
    } else if (event->type() == QEvent::DragMove) {
        // ...
        return KXmlGuiWindow::eventFilter(obj, event);
    } else if (event->type() == QEvent::Drop) {
        // intercept and block native drop
    }
```
Let's check if the native `QTreeView` and `QListView` accept the drag actions!
Yes, they both have `setDragDropMode(QAbstractItemView::DragDrop)`!
So they WILL show the drop indicators natively!
And they WILL accept the `DragMove` events natively!
And when `Drop` happens, my `eventFilter` will catch it, do the DB stuff, do `takeRow`/`appendRow`, and `return true` to prevent native `QTreeView` from modifying the model!

This is brilliantly simple and it will fix the visual feedback AND the drag/drop issues!
Let's modify `MainWindow::eventFilter` to pass `DragEnter` and `DragMove` to the default handler!
