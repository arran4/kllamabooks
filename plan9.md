Ah, the user is saying they want a way of tracking "unread" - i.e. things modified *without* viewing, even if it's done via a notification.
Wait, earlier I used `review_needed`. But `review_needed` is specifically for AI changes *that are pending review*. Once reviewed, it's gone.
If the AI modifies it and we view it, maybe it clears the notification?
Wait, if they want "unread" state, how is that represented? We can add a new notification type "unread" or "modified". But wait, the prompt asks: "I want a way of tracking 'unread' here, ie things modified without viewing even if it's done via a notification".

Let's read `MainWindow::getDocumentContent`. If we open a document, we can clear the "unread" notification!
Wait, when is the document *modified* by AI?
If it's modified directly by an AI (e.g. without review?) No, the AI changes queue it for review. Once reviewed (in `DocumentReviewDialog`), if it is accepted, the document is modified. At that point, is it "unread"?
The user says: "things modified without viewing even if it's done via a notification".
If it's modified without viewing, maybe we add an `unread` notification? Or maybe `responded_to`?
Wait! `responded_to` is what chat nodes get! `QueueManager::onComplete` adds `responded_to` for chat nodes.
When a document is modified by AI, does it get `responded_to`? No, it gets `review_needed`.
If the user reviews it and accepts it, `DocumentReviewDialog` edits the document. Does it add an "unread" notification? The dialog itself is a view. So you *did* view it if you accepted it!

Wait... "even if it's done via a notification". What does this mean?
Maybe they mean that when you *accept* the review from the notification (which opens the DocumentReviewDialog), you are viewing it. BUT what if a background task modifies a document *without* user review? Does that happen? No.
Maybe they just want an "unread" notification when the AI generation finishes, and we should just clear that "unread" notification when the user clicks on the document in the tree view?

Let's ask the user to clarify or let me check `QueueManager::onComplete`.
Actually, the user says "I want a way of tracking 'unread' here, ie things modified without viewing even if it's done via a notification".
If I click on the document in the tree view, it calls `onOpenBooksTreeDoubleClicked` or `onOpenBooksSelectionChanged`.
