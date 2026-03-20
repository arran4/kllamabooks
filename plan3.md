Wait, in `onBookSelected`, we have:
```cpp
    if (m_openDatabases.contains(fileName)) {
        currentDb = m_openDatabases[fileName];
    } else {
        // ... loads database
    }
```
If it's already open, we should just find the `bookItem` in `openBooksModel` and select it, rather than re-adding it!
So:
```cpp
    if (m_openDatabases.contains(fileName)) {
        currentDb = m_openDatabases[fileName];
        QueueManager::instance().setActiveDatabase(currentDb);
        // Find bookItem in openBooksModel
        for (int i = 0; i < openBooksModel->rowCount(); ++i) {
            if (openBooksModel->item(i)->text() == fileName) {
                openBooksTree->setCurrentIndex(openBooksModel->item(i)->index());
                break;
            }
        }
        return;
    }
```
Wait, if it returns, it doesn't remove it from `bookList`? But if it's open, why is it in `bookList`? It shouldn't be!
Wait, `bookList` is populated in `loadBooks`. Does `loadBooks` exclude already open books?
Let's check `loadBooks`.
