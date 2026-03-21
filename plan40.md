Oh! The error is:
```
/home/arran/Documents/Projects/kllamabooks/src/MainWindow.cpp:2191:21: error: ‘sourceView’ was not declared in this scope
2191 |                    (sourceView == openBooksTree || sourceView == vfsExplorer)) {
```
Ah! I defined `sourceView` INSIDE the previous block, or I didn't define it in the scope where it's used!
Let's check the code:
```cpp
            QWidget* sourceWidget = qobject_cast<QWidget*>(dropEvent->source());
            QAbstractItemView* sourceView = qobject_cast<QAbstractItemView*>(sourceWidget);
            if (!sourceView && sourceWidget && sourceWidget->parentWidget()) {
                sourceView = qobject_cast<QAbstractItemView*>(sourceWidget->parentWidget());
            }

        } else if ((obj == openBooksTree || obj == openBooksTree->viewport() || obj == vfsExplorer || obj == vfsExplorer->viewport()) &&
                   (sourceView == openBooksTree || sourceView == vfsExplorer)) {
```
Wait! `sourceView` was declared inside the FIRST block? No, I put it right before the `else if`?!
Wait, `} else if ...` is how `if` statements work. If I put code between `}` and `else if`, it's a syntax error, UNLESS I accidentally put it inside the previous `if` block!
Let's see:
```cpp
        } else if (obj == vfsExplorer || obj == vfsExplorer->viewport()) {
            // ...
            } else {
                dragEvent->acceptProposedAction();
                return true;
            }

            QWidget* sourceWidget = qobject_cast<QWidget*>(dropEvent->source());
            QAbstractItemView* sourceView = qobject_cast<QAbstractItemView*>(sourceWidget);
            if (!sourceView && sourceWidget && sourceWidget->parentWidget()) {
                sourceView = qobject_cast<QAbstractItemView*>(sourceWidget->parentWidget());
            }

        } else if ((obj == openBooksTree || obj == openBooksTree->viewport() || obj == vfsExplorer || obj == vfsExplorer->viewport()) &&
                   (sourceView == openBooksTree || sourceView == vfsExplorer)) {
```
YES! I put the definition of `sourceView` INSIDE the `else if (obj == vfsExplorer || obj == vfsExplorer->viewport())` block!
So `sourceView` is NOT declared in the outer scope, and the outer `else if` condition cannot see it!

Also:
```
/home/arran/Documents/Projects/kllamabooks/src/MainWindow.cpp:2192:104: error: conditional expression between distinct pointer types ‘QTreeView*’ and ‘QListView*’ lacks a cast
2192 |             QAbstractItemView* targetView = (obj == openBooksTree || obj == openBooksTree->viewport()) ? openBooksTree : vfsExplorer;
```
Ah! `openBooksTree` is a `QTreeView*`, `vfsExplorer` is a `QListView*`.
The ternary operator `? :` requires them to have a common type! C++ does not automatically cast them to a common base class pointer (like `QAbstractItemView*`) in a ternary unless explicitly casted!
So it needs to be `? static_cast<QAbstractItemView*>(openBooksTree) : static_cast<QAbstractItemView*>(vfsExplorer);`!

Let's fix both of these compilation errors.
