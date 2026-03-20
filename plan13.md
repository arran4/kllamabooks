Wait!
"Items in the tree view and list view that have notifications should display differently"
`NotificationDelegate` uses `notifyType` = role `Qt::UserRole + 10`.
But is it actually used properly?
In `MainWindow::updateNotificationStatus`:
```cpp
    // Live update tree markers
    for (int i = 0; i < openBooksModel->rowCount(); ++i) {
        QStandardItem* bookItem = openBooksModel->item(i);
        QString filePath = bookItem->data(Qt::UserRole + 2).toString();
        ...
            auto notifications = m_openDatabases[fileName]->getNotifications();
            updateTreeMarkersRecursive(bookItem, notifications);
            updateVfsMarkers(notifications);
```
Wait, the `notifyType` role is set for the items. And `NotificationDelegate` uses it to draw a green/red circle.
If it's already set up and drawing circles, why does the issue say "Items in the tree view and list view that have notifications should display differently"?
Maybe because the text itself should be bolded, or highlighted? The dot might be hard to see?
If I look at `NotificationDelegate::paint`:
```cpp
    int notifyType = index.data(Qt::UserRole + 10).toInt();
    if (notifyType == 0) return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    QRect rect = option.rect;
    int dotSize = 10;
    // Position dot in the top right corner of the item
    QRect dotRect(rect.right() - dotSize - 4, rect.top() + 4, dotSize, dotSize);
```
Wait! `QStyledItemDelegate::paint` is called FIRST. Then it draws the dot!
But if we want them to display differently, maybe we should make the font bold?
Yes! "should display differently" - making the font bold is a very common way to show unread notifications.
Let's modify `NotificationDelegate` to make the font bold, or change the background.
If we set `option.font.setBold(true)`, we have to do it BEFORE `QStyledItemDelegate::paint`!
But `option` is `const QStyleOptionViewItem&`, we can't modify it easily. We can copy it!
```cpp
void NotificationDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    int notifyType = index.data(Qt::UserRole + 10).toInt();
    QStyleOptionViewItem opt = option;
    if (notifyType != 0) {
        opt.font.setBold(true);
    }
    QStyledItemDelegate::paint(painter, opt, index);

    if (notifyType == 0) return;
    ...
```
