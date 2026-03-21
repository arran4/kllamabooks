If `QTreeView` passes drag-and-drop events to its viewport, we MUST check `obj == openBooksTree->viewport()`!
```cpp
    openBooksTree->viewport()->installEventFilter(this);
    vfsExplorer->viewport()->installEventFilter(this);
    bookList->viewport()->installEventFilter(this);
```
And in `eventFilter`:
```cpp
    if (obj == openBooksTree || obj == openBooksTree->viewport()) {
        // Handle it
    }
```
Let's see `eventFilter`:
```cpp
        if (obj == openBooksTree && dragEvent->source() == bookList) {
```
If we change `obj == openBooksTree` to `obj == openBooksTree || obj == openBooksTree->viewport()`.
Wait, `dragEvent->source()` returns the widget where the drag started!
Does it return `openBooksTree` or its `viewport()`?
It usually returns the view (`openBooksTree`), not the viewport.
So `dropEvent->source() == openBooksTree` is correct.
But `obj` is the target of the event!
So `obj` will be the viewport!
Let's replace `obj == openBooksTree` with `(obj == openBooksTree || obj == openBooksTree->viewport())` everywhere in `eventFilter`!
And `obj == vfsExplorer` with `(obj == vfsExplorer || obj == vfsExplorer->viewport())`!
And `obj == bookList` with `(obj == bookList || obj == bookList->viewport())`!
