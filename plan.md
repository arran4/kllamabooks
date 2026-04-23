1. **Update `src/MainWindow.cpp`**:
    - Keep `newContextItemAction` without a `QMenu`. This ensures it remains a clickable item in standard menus.
    - Create a new action `newContextItemToolbarAction` that shares the same icon, text, and trigger slot as `newContextItemAction`.
    - Attach the `contextMenu` (containing Chat, Document, etc.) to `newContextItemToolbarAction`.
    - Register `newContextItemToolbarAction` with `actionCollection()->addAction(QStringLiteral("new_context_item_toolbar"), newContextItemToolbarAction);`.
2. **Update `src/kllamabooksui.rc`**:
    - In `<Menu name="file">`, move `<Action name="new_context_item" />` to be the first item inside the `<Menu name="new_items">` (the "New" submenu). This groups all "New" actions logically and removes the confusion of having both a "New" menu and a "New Context Item" directly in File.
    - In the `<ToolBar name="mainToolBar">`, replace `<Action name="new_context_item" />` with `<Action name="new_context_item_toolbar" />`. This gives the toolbar button the required dropdown without breaking the File menu.
3. **Run `cppcheck`** to ensure no syntax issues.
4. **Submit** the changes.
