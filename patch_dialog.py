import re

with open("src/MainWindow.cpp", "r") as f:
    content = f.read()

replacement = """
            if (found) {
                QDialog dialog(this);
                dialog.setWindowTitle("Document Generated");
                QVBoxLayout* layout = new QVBoxLayout(&dialog);

                QLabel* label = new QLabel("The AI has finished generating. Review or modify the output below:");
                layout->addWidget(label);

                QTextEdit* textEdit = new QTextEdit(&dialog);
                textEdit->setPlainText(targetQ.response);
                layout->addWidget(textEdit);

                QDialogButtonBox* buttonBox = new QDialogButtonBox(Qt::Horizontal, &dialog);
                QPushButton* replaceBtn = buttonBox->addButton("Replace", QDialogButtonBox::ActionRole);
                QPushButton* appendBtn = buttonBox->addButton("Append", QDialogButtonBox::ActionRole);
                QPushButton* createBtn = buttonBox->addButton("Create New Fork", QDialogButtonBox::ActionRole);
                QPushButton* regenerateBtn = buttonBox->addButton("Regenerate", QDialogButtonBox::ActionRole);
                QPushButton* discardBtn = buttonBox->addButton("Discard", QDialogButtonBox::RejectRole);

                layout->addWidget(buttonBox);

                QObject::connect(replaceBtn, &QPushButton::clicked, [&]() { dialog.done(1); });
                QObject::connect(appendBtn, &QPushButton::clicked, [&]() { dialog.done(2); });
                QObject::connect(createBtn, &QPushButton::clicked, [&]() { dialog.done(3); });
                QObject::connect(regenerateBtn, &QPushButton::clicked, [&]() { dialog.done(4); });
                QObject::connect(discardBtn, &QPushButton::clicked, [&]() { dialog.reject(); });

                int result = dialog.exec();

                if (result == 1) { // Replace
                    QList<DocumentNode> docs = currentDb->getDocuments();
                    for (const auto& doc : docs) {
                        if (doc.id == docId) {
                            currentDb->updateDocument(docId, doc.title, textEdit->toPlainText());
                            break;
                        }
                    }
                    currentDb->deleteQueueItem(targetQ.id);
                    currentDb->dismissNotificationByMessageId(docId);
                    loadDocumentsAndNotes();
                } else if (result == 2) { // Append
                    QList<DocumentNode> docs = currentDb->getDocuments();
                    for (const auto& doc : docs) {
                        if (doc.id == docId) {
                            currentDb->updateDocument(docId, doc.title, doc.content + "\\n" + textEdit->toPlainText());
                            break;
                        }
                    }
                    currentDb->deleteQueueItem(targetQ.id);
                    currentDb->dismissNotificationByMessageId(docId);
                    loadDocumentsAndNotes();
                } else if (result == 3) { // Create New Fork
                    QList<DocumentNode> docs = currentDb->getDocuments();
                    int folderId = 0;
                    QString title = "New Document";
                    for (const auto& doc : docs) {
                        if (doc.id == docId) {
                            folderId = doc.folderId;
                            title = doc.title;
                            break;
                        }
                    }
                    currentDb->addDocument(folderId, title + " (Generated)", textEdit->toPlainText(), docId);
                    currentDb->deleteQueueItem(targetQ.id);
                    currentDb->dismissNotificationByMessageId(docId);
                    loadDocumentsAndNotes();
                } else if (result == 4) { // Regenerate
                    AiActionDialog regenDialog("Regenerate Document", "Edit the prompt for AI generation:", targetQ.prompt, "", this);
                    if (regenDialog.exec() == QDialog::Accepted) {
                        QString newPrompt = regenDialog.getPrompt();
                        if (!newPrompt.isEmpty()) {
                            currentDb->deleteQueueItem(targetQ.id);
                            currentDb->dismissNotificationByMessageId(docId);
                            QueueManager::instance().enqueuePrompt(docId, targetQ.model, newPrompt, 0, "document");
                            statusBar->showMessage(tr("AI document regeneration task queued."), 3000);
                        }
                    }
                    loadDocumentsAndNotes();
                } else if (result == QDialog::Rejected) { // Discard
                    currentDb->deleteQueueItem(targetQ.id);
                    currentDb->dismissNotificationByMessageId(docId);
                    loadDocumentsAndNotes();
                }
                return;
            }
"""

content = re.sub(
    r'if \(found\) \{\s*QMessageBox msgBox\(this\);.*?return;\s*\}',
    replacement,
    content,
    flags=re.DOTALL
)

with open("src/MainWindow.cpp", "w") as f:
    f.write(content)
