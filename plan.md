1. **Understand Request**:
   - The user wants more details about queue items (errors, full prompt, etc.) shown somewhere, since the list item text is not enough.
   - They suggest making it a custom list entry or another form of widget.
   - Specifically, they suggest a "more info" context menu action.
   - If we add a context menu to the queue list, we should move all non-bulk actions (Jump, Retry, Modify, Delete) into this context menu, cleaning up the toolbar. The toolbar might only keep bulk actions like "Clear Completed" or "Pause/Resume Queue".

2. **Actions**:
   - Add a context menu to `m_queueList` in `QueueWindow`.
   - The context menu will have: "Jump to Target", "Show Details...", "Modify Prompt", "Retry", "Delete".
   - Remove these individual buttons (`m_jumpBtn`, `m_modifyBtn`, `m_retryBtn`, `m_cancelBtn`) from the `btnLayout` and instead make them actions in the context menu.
   - Implement "Show Details...": pops up a `QMessageBox` or custom `QDialog` showing all `QueueItem` fields: ID, target type, target title, state, full prompt, current response/error, model, priority.
   - Set up `m_queueList->setContextMenuPolicy(Qt::CustomContextMenu);`.
   - Connect `customContextMenuRequested` to a slot `showContextMenu(const QPoint& pos)`.

3. **Modify `QueueWindow.h`**:
   - Add `void showContextMenu(const QPoint& pos);`
   - Add `void onShowDetails();`
   - Remove `m_cancelBtn`, `m_retryBtn`, `m_modifyBtn`, `m_jumpBtn`.
   - The toolbar will only have `Move Up`, `Move Down`, `Pause Queue`, `Clear Completed`. (Maybe Move Up/Down are per-item, but they were disabled anyway).

4. **Modify `QueueWindow.cpp`**:
   - Remove instantiation of removed buttons.
   - Implement `showContextMenu(const QPoint& pos)`:
     ```cpp
     QMenu menu(this);
     auto item = m_queueList->itemAt(pos);
     if (item) {
         menu.addAction("Jump to Target", this, &QueueWindow::onJumpItem);
         menu.addAction("Show Details...", this, &QueueWindow::onShowDetails);
         // only add retry if error
         // add modify
         menu.addAction("Modify Prompt...", this, &QueueWindow::onModifyItem);
         menu.addAction("Retry", this, &QueueWindow::onRetryItem);
         menu.addAction("Delete", this, &QueueWindow::onCancelItem);
         menu.exec(m_queueList->mapToGlobal(pos));
     }
     ```
   - Implement `onShowDetails()`:
     - Find `QueueItem` just like in other slots.
     - Show a `QDialog` or `QMessageBox` with full details. A custom dialog with `QTextEdit` for the prompt/error is best.
