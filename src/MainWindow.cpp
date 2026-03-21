#include "MainWindow.h"

#include <KActionCollection>
#include <KActionMenu>
#include <KStandardAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDate>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QGuiApplication>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QLocale>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPointer>
#include <QProcess>
#include <QScreen>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStyle>
#include <QTextBlock>
#include <QUrl>
#include <QVBoxLayout>
#include <QtGui/QAction>
#include <limits>

#include "DatabaseSettingsDialog.h"
#include "ModelSelectionDialog.h"
#include "NotificationDelegate.h"
#include "QueueManager.h"
#include "QueueWindow.h"
#include "WalletManager.h"

MainWindow::MainWindow(QWidget* parent) : KXmlGuiWindow(parent), currentLastNodeId(0) {
    QueueManager::instance().setClient(&ollamaClient);
    connect(&QueueManager::instance(), &QueueManager::queueChanged, this, &MainWindow::updateQueueStatus);
    connect(&QueueManager::instance(), &QueueManager::queueChanged, this, &MainWindow::updateNotificationStatus);
    connect(&QueueManager::instance(), &QueueManager::processingChunk, this, &MainWindow::onQueueChunk);
    connect(&QueueManager::instance(), &QueueManager::processingStarted, this, &MainWindow::onProcessingStarted);
    connect(&QueueManager::instance(), &QueueManager::processingFinished, this, &MainWindow::onProcessingFinished);

    setupUi();
    setupWindow();

    openBooksTree->setItemDelegate(new NotificationDelegate(this));
    vfsExplorer->setItemDelegate(new NotificationDelegate(this));
    chatForkExplorer->setItemDelegate(new NotificationDelegate(this));
}

MainWindow::~MainWindow() {}

void MainWindow::closeEvent(QCloseEvent* event) {
    QSettings settings;
    settings.setValue("geometry", saveGeometry());
    settings.setValue("splitterState", splitter->saveState());
    settings.setValue("leftSplitterState", leftSplitter->saveState());

    QStringList openBooks;
    for (int i = 0; i < openBooksModel->rowCount(); ++i) {
        openBooks.append(openBooksModel->item(i)->text());
    }
    settings.setValue("openBooks", openBooks);

    KXmlGuiWindow::closeEvent(event);
}

void MainWindow::setupUi() {
    splitter = new QSplitter(this);
    setCentralWidget(splitter);

    leftSplitter = new QSplitter(Qt::Vertical, this);

    openBooksTree = new QTreeView(this);
    openBooksModel = new QStandardItemModel(this);
    openBooksTree->setModel(openBooksModel);
    openBooksTree->setHeaderHidden(true);
    openBooksTree->setAcceptDrops(true);
    openBooksTree->setDragEnabled(true);
    openBooksTree->setDragDropMode(QAbstractItemView::DragDrop);
    openBooksTree->setDefaultDropAction(Qt::MoveAction);
    openBooksTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    openBooksTree->setDropIndicatorShown(true);
    openBooksTree->installEventFilter(this);
    openBooksTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(openBooksTree, &QWidget::customContextMenuRequested, this, &MainWindow::showOpenBookContextMenu);

    connect(openBooksTree, &QTreeView::doubleClicked, this, [this](const QModelIndex& index) {
        QStandardItem* item = openBooksModel->itemFromIndex(index);
        if (item) {
            QString type = item->data(Qt::UserRole + 1).toString();
            if (type == "document" || type == "template" || type == "draft") {
                int docId = item->data(Qt::UserRole).toInt();
                if (currentDb) {
                    QList<DocumentNode> docs;
                    if (type == "document")
                        docs = currentDb->getDocuments();
                    else if (type == "template")
                        docs = currentDb->getTemplates();
                    else if (type == "draft")
                        docs = currentDb->getDrafts();

                    for (const auto& doc : docs) {
                        if (doc.id == docId) {
                            currentDocumentId = docId;
                            documentEditorView->setPlainText(doc.content);
                            mainContentStack->setCurrentWidget(docContainer);
                            break;
                        }
                    }
                }
            } else if (item->data(Qt::UserRole).toInt() > 0 && type != "doc_folder" && type != "templates_folder" &&
                       type != "drafts_folder") {
                // It's a chat node. Single clicking displays the chat.
                currentLastNodeId = item->data(Qt::UserRole).toInt();
                if (currentDb) {
                    updateLinearChatView(currentLastNodeId, currentDb->getMessages());
                    mainContentStack->setCurrentWidget(chatWindowView);
                }
            }
        }
    });

    bookList = new QListWidget(this);
    bookList->setAcceptDrops(true);
    bookList->setDragEnabled(true);
    bookList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    bookList->setDragDropMode(QAbstractItemView::DragDrop);
    bookList->installEventFilter(this);
    bookList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(bookList, &QWidget::customContextMenuRequested, this, &MainWindow::showBookContextMenu);

    leftSplitter->addWidget(openBooksTree);
    leftSplitter->addWidget(bookList);

    // Initial size of left splitter
    leftSplitter->setStretchFactor(0, 3);  // 60% approx
    leftSplitter->setStretchFactor(1, 2);  // 40% approx

    splitter->addWidget(leftSplitter);
    mainContentStack = new QStackedWidget(this);

    emptyView = new QWidget(this);
    mainContentStack->addWidget(emptyView);

    vfsExplorer = new QListView(this);
    vfsModel = new QStandardItemModel(this);
    vfsExplorer->setModel(vfsModel);
    vfsExplorer->setViewMode(QListView::IconMode);
    vfsExplorer->setAcceptDrops(true);
    vfsExplorer->setDragEnabled(true);
    vfsExplorer->setDragDropMode(QAbstractItemView::DragDrop);
    vfsExplorer->setDefaultDropAction(Qt::MoveAction);
    vfsExplorer->setEditTriggers(QAbstractItemView::NoEditTriggers);
    vfsExplorer->setContextMenuPolicy(Qt::CustomContextMenu);
    vfsExplorer->installEventFilter(this);
    mainContentStack->addWidget(vfsExplorer);

    connect(vfsExplorer, &QListView::doubleClicked, this, [this](const QModelIndex& index) {
        if (!index.isValid()) return;

        QStandardItem* item = vfsModel->itemFromIndex(index);
        if (!item) return;

        if (item->text() == "..") {
            QModelIndex currentIndex = openBooksTree->currentIndex();
            if (currentIndex.isValid() && currentIndex.parent().isValid()) {
                openBooksTree->setCurrentIndex(currentIndex.parent());
            }
            return;
        }

        QString type = item->data(Qt::UserRole + 1).toString();
        int nodeId = item->data(Qt::UserRole).toInt();

        std::function<QStandardItem*(QStandardItem*)> findNode = [&](QStandardItem* parent) -> QStandardItem* {
            for (int i = 0; i < parent->rowCount(); ++i) {
                QStandardItem* child = parent->child(i);
                if (child->data(Qt::UserRole).toInt() == nodeId && child->data(Qt::UserRole + 1).toString() == type) {
                    return child;
                }
                QStandardItem* found = findNode(child);
                if (found) return found;
            }
            return nullptr;
        };

        QStandardItem* found = nullptr;
        if (openBooksModel && openBooksModel->rowCount() > 0) {
            for (int i = 0; i < openBooksModel->rowCount(); ++i) {
                found = findNode(openBooksModel->item(i));
                if (found) break;
            }
        }

        if (found) {
            openBooksTree->setCurrentIndex(found->index());
        }
    });

    chatModel = new QStandardItemModel(this);
    chatTree = openBooksTree;

    docContainer = new QWidget(this);
    QVBoxLayout* docLayout = new QVBoxLayout(docContainer);
    documentEditorView = new QTextEdit(this);
    saveDocBtn = new QPushButton("Save Document", this);
    QPushButton* backToDocsBtn = new QPushButton("Back to Documents", this);
    QHBoxLayout* docBtnLayout = new QHBoxLayout();
    docBtnLayout->addWidget(backToDocsBtn);
    docBtnLayout->addWidget(saveDocBtn);
    docLayout->addWidget(documentEditorView);
    docLayout->addLayout(docBtnLayout);
    mainContentStack->addWidget(docContainer);

    noteContainer = new QWidget(this);
    QVBoxLayout* noteLayout = new QVBoxLayout(noteContainer);
    noteEditorView = new QTextEdit(this);

    QTimer* draftTimer = new QTimer(this);
    draftTimer->setSingleShot(true);
    draftTimer->setInterval(2000);
    connect(documentEditorView, &QTextEdit::textChanged, this, [draftTimer, this]() {
        if (documentEditorView->hasFocus()) draftTimer->start();
    });
    connect(noteEditorView, &QTextEdit::textChanged, this, [draftTimer, this]() {
        if (noteEditorView->hasFocus()) draftTimer->start();
    });
    saveNoteBtn = new QPushButton("Save Note", this);
    QPushButton* backToNotesBtn = new QPushButton("Back to Notes", this);
    QHBoxLayout* noteBtnLayout = new QHBoxLayout();
    noteBtnLayout->addWidget(backToNotesBtn);
    noteBtnLayout->addWidget(saveNoteBtn);
    noteLayout->addWidget(noteEditorView);
    noteLayout->addLayout(noteBtnLayout);
    mainContentStack->addWidget(noteContainer);

    connect(backToDocsBtn, &QPushButton::clicked, this, [this]() { mainContentStack->setCurrentWidget(emptyView); });

    connect(backToNotesBtn, &QPushButton::clicked, this, [this]() { mainContentStack->setCurrentWidget(emptyView); });

    // Remove old folder view connections

    connect(draftTimer, &QTimer::timeout, this, [this]() {
        if (!currentDb) return;
        QModelIndex index = openBooksTree->currentIndex();
        QStandardItem* item = index.isValid() ? openBooksModel->itemFromIndex(index) : nullptr;
        if (!item || item->data(Qt::UserRole + 1).toString() == "draft") return;

        QString title = item->text();
        if (title == "*New Item*")
            title = "Unsaved Draft";
        else
            title = "Draft of: " + title;

        if (currentAutoDraftId == 0) {
            currentAutoDraftId = currentDb->addDraft(
                0, title,
                documentEditorView->hasFocus() ? documentEditorView->toPlainText() : noteEditorView->toPlainText());
        } else {
            currentDb->updateDraft(
                currentAutoDraftId, title,
                documentEditorView->hasFocus() ? documentEditorView->toPlainText() : noteEditorView->toPlainText());
        }
    });

    connect(saveDocBtn, &QPushButton::clicked, this, [this]() {
        if (!currentDb) return;
        QModelIndex index = openBooksTree->currentIndex();
        QStandardItem* item = index.isValid() ? openBooksModel->itemFromIndex(index) : nullptr;
        QString currentTitle = item ? item->text() : "New Document";
        if (currentTitle == "*New Item*") currentTitle = "Untitled Document";

        if (isCreatingNewDoc && item) {
            int folderId = 0;
            if (item->parent()) {
                folderId = item->parent()->data(Qt::UserRole).toInt();
            }
            int newId = 0;
            QString type = item->data(Qt::UserRole + 1).toString();
            if (type == "document")
                newId = currentDb->addDocument(folderId, currentTitle, documentEditorView->toPlainText());
            else if (type == "template")
                newId = currentDb->addTemplate(folderId, currentTitle, documentEditorView->toPlainText());
            else if (type == "draft")
                newId = currentDb->addDraft(folderId, currentTitle, documentEditorView->toPlainText());
            else
                newId = currentDb->addDocument(folderId, currentTitle, documentEditorView->toPlainText());

            statusBar->showMessage(tr("Document saved."), 3000);
            isCreatingNewDoc = false;
            if (currentAutoDraftId != 0) {
                currentDb->deleteDraft(currentAutoDraftId);
                currentAutoDraftId = 0;
            }
            loadDocumentsAndNotes();
            // Restore selection to the new item
            QStandardItem* newItem = findItemInTree(newId, type);
            if (newItem) {
                openBooksTree->setCurrentIndex(newItem->index());
            }
            return;
        }
        if (currentDocumentId == 0) return;
        QString type = item ? item->data(Qt::UserRole + 1).toString() : "document";
        if (type == "document")
            currentDb->updateDocument(currentDocumentId, currentTitle, documentEditorView->toPlainText());
        else if (type == "template")
            currentDb->updateTemplate(currentDocumentId, currentTitle, documentEditorView->toPlainText());
        else if (type == "draft")
            currentDb->updateDraft(currentDocumentId, currentTitle, documentEditorView->toPlainText());

        if (currentAutoDraftId != 0) {
            currentDb->deleteDraft(currentAutoDraftId);
            currentAutoDraftId = 0;
        }
        statusBar->showMessage(tr("Document saved."), 3000);
        loadDocumentsAndNotes();  // Refresh tree to show new title
    });

    connect(saveNoteBtn, &QPushButton::clicked, this, [this]() {
        if (!currentDb) return;
        QModelIndex index = openBooksTree->currentIndex();
        QStandardItem* item = index.isValid() ? openBooksModel->itemFromIndex(index) : nullptr;
        QString currentTitle = item ? item->text() : "New Note";
        if (currentTitle == "*New Item*") currentTitle = "Untitled Note";

        if (isCreatingNewNote && item) {
            int folderId = 0;
            if (item->parent()) {
                folderId = item->parent()->data(Qt::UserRole).toInt();
            }
            int newId = currentDb->addNote(folderId, currentTitle, noteEditorView->toPlainText());
            statusBar->showMessage(tr("Note saved."), 3000);
            isCreatingNewNote = false;
            loadDocumentsAndNotes();
            // Restore selection to the new item
            QStandardItem* newItem = findItemInTree(newId, "note");
            if (newItem) {
                openBooksTree->setCurrentIndex(newItem->index());
            }
            return;
        }
        if (currentNoteId == 0) return;
        currentDb->updateNote(currentNoteId, currentTitle, noteEditorView->toPlainText());
        statusBar->showMessage(tr("Note saved."), 3000);
        loadDocumentsAndNotes();  // Refresh tree to show new title
    });

    chatWindowView = new QWidget(this);
    QVBoxLayout* chatLayout = new QVBoxLayout(chatWindowView);
    chatLayout->setContentsMargins(0, 0, 0, 0);

    chatSplitter = new QSplitter(Qt::Vertical, this);

    chatInputContainer = new QWidget(this);
    QVBoxLayout* chatInputLayout = new QVBoxLayout(chatInputContainer);
    chatInputLayout->setContentsMargins(0, 0, 0, 0);

    chatTextArea = new QTextEdit(this);
    chatTextArea->setReadOnly(true);
    chatTextArea->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(chatTextArea, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu* menu = chatTextArea->createStandardContextMenu();
        menu->addSeparator();
        QAction* saveDraftAction = menu->addAction(QIcon::fromTheme("document-save-as"), "Save chat as draft");
        QAction* saveTemplateAction = menu->addAction(QIcon::fromTheme("document-save-as"), "Save chat as template");

        QAction* selectedAction = menu->exec(chatTextArea->viewport()->mapToGlobal(pos));
        if (selectedAction == saveDraftAction) {
            bool ok;
            QString name =
                QInputDialog::getText(this, "Save as Draft", "Draft Name:", QLineEdit::Normal, "New Draft", &ok);
            if (ok && !name.isEmpty() && currentDb) {
                currentDb->addDraft(0, name, chatTextArea->toPlainText());
                loadDocumentsAndNotes();
            }
        } else if (selectedAction == saveTemplateAction) {
            bool ok;
            QString name = QInputDialog::getText(this, "Save as Template", "Template Name:", QLineEdit::Normal,
                                                 "New Template", &ok);
            if (ok && !name.isEmpty() && currentDb) {
                currentDb->addTemplate(0, name, chatTextArea->toPlainText());
                loadDocumentsAndNotes();
            }
        }
        delete menu;
    });
    chatTextArea->setStyleSheet(
        "QTextEdit { background-color: #f9f9f9; border: 1px solid #ccc; border-radius: 4px; padding: 10px; }");
    // Left native standard context menu for basic copy/paste without block mapping
    chatInputLayout->addWidget(chatTextArea);

    chatSplitter->addWidget(chatInputContainer);

    forkExplorerModel = new QStandardItemModel(this);
    chatForkExplorer = new QListView(this);
    chatForkExplorer->setModel(forkExplorerModel);
    chatForkExplorer->setViewMode(QListView::IconMode);
    chatForkExplorer->setEditTriggers(QAbstractItemView::NoEditTriggers);

    connect(chatForkExplorer, &QListView::doubleClicked, this, [this](const QModelIndex& index) {
        if (!index.isValid()) return;
        QStandardItem* item = forkExplorerModel->itemFromIndex(index);
        if (!item) return;

        int nodeId = item->data(Qt::UserRole).toInt();
        if (nodeId != currentLastNodeId) {
            currentLastNodeId = nodeId;
            if (currentDb) {
                updateLinearChatView(currentLastNodeId, currentDb->getMessages());
                chatTextArea->setFocus();
            }
        }
    });

    chatForkExplorer->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(chatForkExplorer, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(this);
        QModelIndex index = chatForkExplorer->indexAt(pos);

        QAction* newChatAction = menu.addAction(QIcon::fromTheme("chat-message-new"), "New Chat");
        QAction* importAction = menu.addAction(QIcon::fromTheme("document-import"), "Import Chat Session");
        menu.addSeparator();

        QAction* copyAction = nullptr;
        QAction* forkAction = nullptr;

        if (index.isValid()) {
            copyAction = menu.addAction(QIcon::fromTheme("edit-copy"), "Copy Message");
            forkAction = menu.addAction(QIcon::fromTheme("call-start"), "Fork from Here");
        }

        QAction* selectedAction = menu.exec(chatForkExplorer->viewport()->mapToGlobal(pos));
        if (selectedAction == newChatAction) {
            // Find chats folder item in the main tree
            QStandardItem* chatsFolder = nullptr;
            if (openBooksModel && openBooksModel->rowCount() > 0) {
                QStandardItem* root = openBooksModel->item(0);
                for (int i = 0; i < root->rowCount(); ++i) {
                    if (root->child(i)->data(Qt::UserRole + 1).toString() == "chats_folder") {
                        chatsFolder = root->child(i);
                        break;
                    }
                }
            }
            if (chatsFolder) addPhantomItem(chatsFolder, "chats_folder");
        } else if (selectedAction == importAction) {
            importChatSession();
        } else if (index.isValid() && selectedAction == copyAction) {
            QStandardItem* item = forkExplorerModel->itemFromIndex(index);
            if (item) QApplication::clipboard()->setText(item->text());
        } else if (index.isValid() && selectedAction == forkAction) {
            QStandardItem* item = forkExplorerModel->itemFromIndex(index);
            if (item) {
                int nodeId = item->data(Qt::UserRole).toInt();
                currentLastNodeId = nodeId;
                if (currentDb) updateLinearChatView(currentLastNodeId, currentDb->getMessages());
            }
        }
    });

    chatSplitter->addWidget(chatForkExplorer);
    chatLayout->addWidget(chatSplitter);

    QHBoxLayout* inputLayout = new QHBoxLayout();
    modelSelectButton = new QPushButton(QIcon::fromTheme("system-search"), "Select Model", this);
    inputLayout->addWidget(modelSelectButton);

    toggleInputModeBtn = new QPushButton(QIcon::fromTheme("format-text-strikethrough"), "", this);
    toggleInputModeBtn->setToolTip(tr("Toggle Multi-line Input"));
    toggleInputModeBtn->setCheckable(true);
    inputLayout->addWidget(toggleInputModeBtn);

    inputModeStack = new QStackedWidget(this);
    inputField = new ChatInputWidget(this);
    // HEAD input settings
    inputModeStack->addWidget(inputField);

    multiLineInput = new QTextEdit(this);
    multiLineInput->setMaximumHeight(100);
    inputModeStack->addWidget(multiLineInput);

    inputLayout->addWidget(inputModeStack);

    discardChangesBtn = new QPushButton("Discard Changes", this);
    discardChangesBtn->setToolTip(tr("Discard any pending edits to history."));
    discardChangesBtn->hide();  // Initially hidden until an edit happens
    inputLayout->addWidget(discardChangesBtn);

    connect(chatTextArea, &QTextEdit::textChanged, this, [this]() {
        if (!chatTextArea->isReadOnly() && !m_isGenerating && currentLastNodeId != 0) {
            discardChangesBtn->show();
        }
    });

    sendButton = new QPushButton("Send", this);
    inputSettingsButton = new QToolButton(this);
    inputSettingsButton->setIcon(QIcon::fromTheme("configure"));
    inputSettingsButton->setToolTip(tr("Input Settings"));
    connect(inputSettingsButton, &QToolButton::clicked, this, &MainWindow::showInputSettingsMenu);
    inputLayout->addWidget(sendButton);
    inputLayout->addWidget(inputSettingsButton);

    chatInputLayout->addLayout(inputLayout);

    connect(discardChangesBtn, &QPushButton::clicked, this, &MainWindow::onDiscardChanges);

    mainContentStack->addWidget(chatWindowView);

    QVBoxLayout* btnLayout = new QVBoxLayout();
    QHBoxLayout* topBtnLayout = new QHBoxLayout();
    topBtnLayout->addWidget(sendButton);
    topBtnLayout->addWidget(inputSettingsButton);
    btnLayout->addLayout(topBtnLayout);
    btnLayout->addStretch();  // Push to the top of the container
    inputLayout->addLayout(btnLayout);

    QWidget* rightContainer = new QWidget(this);
    QVBoxLayout* rightLayout = new QVBoxLayout(rightContainer);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    breadcrumbWidget = new QWidget(this);
    breadcrumbLayout = new QHBoxLayout(breadcrumbWidget);
    breadcrumbLayout->setContentsMargins(5, 5, 5, 5);
    breadcrumbWidget->hide();

    rightLayout->addWidget(breadcrumbWidget);
    rightLayout->addWidget(mainContentStack);

    splitter->addWidget(rightContainer);

    connect(openBooksTree->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &MainWindow::onOpenBooksSelectionChanged);
    connect(toggleInputModeBtn, &QPushButton::toggled, this,
            [this](bool checked) { inputModeStack->setCurrentIndex(checked ? 1 : 0); });

    // Initial sizes
    int totalWidth = width();
    int leftWidth = qMax(totalWidth * 20 / 100, 100);
    int rightWidth = totalWidth - leftWidth;
    splitter->setSizes(QList<int>() << leftWidth << rightWidth);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    QToolBar* toolbar = addToolBar("Main");
    toolbar->setObjectName("MainToolBar");
    QAction* newBookAction = new QAction(QIcon::fromTheme("document-new"), tr("New Book"), this);
    QAction* exportChatAction = new QAction(QIcon::fromTheme("document-export"), tr("Export Chat Session"), this);
    actionCollection()->addAction(QStringLiteral("export_chat_session"), exportChatAction);
    connect(exportChatAction, &QAction::triggered, this, &MainWindow::exportChatSession);

    actionCollection()->addAction(QStringLiteral("new_book"), newBookAction);
    toolbar->addAction(newBookAction);

    QAction* openBookAction = new QAction(QIcon::fromTheme("document-open"), tr("Open Book"), this);
    actionCollection()->addAction(QStringLiteral("open_book"), openBookAction);
    connect(openBookAction, &QAction::triggered, this, &MainWindow::onOpenBook);

    QAction* closeBookAction = new QAction(QIcon::fromTheme("document-close"), tr("Close Current Book"), this);
    actionCollection()->addAction(QStringLiteral("close_book"), closeBookAction);
    connect(closeBookAction, &QAction::triggered, this, &MainWindow::onCloseBook);

    QAction* openBookLocationAction = new QAction(QIcon::fromTheme("folder-open"), tr("Open Book Location"), this);
    actionCollection()->addAction(QStringLiteral("open_book_location"), openBookLocationAction);
    connect(openBookLocationAction, &QAction::triggered, this, &MainWindow::onOpenBookLocation);

    QAction* newChatMenuAction = new QAction(QIcon::fromTheme("chat-message-new"), tr("New Chat"), this);
    actionCollection()->addAction(QStringLiteral("new_chat"), newChatMenuAction);
    connect(newChatMenuAction, &QAction::triggered, this, [this]() { addPhantomItem(nullptr, "chats_folder"); });

    QAction* newDocMenuAction = new QAction(QIcon::fromTheme("document-new"), tr("New Document"), this);
    actionCollection()->addAction(QStringLiteral("new_document"), newDocMenuAction);
    connect(newDocMenuAction, &QAction::triggered, this, [this]() { addPhantomItem(nullptr, "docs_folder"); });

    QAction* newNoteMenuAction = new QAction(QIcon::fromTheme("document-new"), tr("New Note"), this);
    actionCollection()->addAction(QStringLiteral("new_note"), newNoteMenuAction);
    connect(newNoteMenuAction, &QAction::triggered, this, [this]() { addPhantomItem(nullptr, "notes_folder"); });

    QAction* newTemplateMenuAction = new QAction(QIcon::fromTheme("document-new"), tr("New Template"), this);
    actionCollection()->addAction(QStringLiteral("new_template"), newTemplateMenuAction);
    connect(newTemplateMenuAction, &QAction::triggered, this,
            [this]() { addPhantomItem(nullptr, "templates_folder"); });

    QAction* newDraftMenuAction = new QAction(QIcon::fromTheme("document-new"), tr("New Draft"), this);
    actionCollection()->addAction(QStringLiteral("new_draft"), newDraftMenuAction);
    connect(newDraftMenuAction, &QAction::triggered, this, [this]() { addPhantomItem(nullptr, "drafts_folder"); });

    QAction* createFolderMenuAction = new QAction(QIcon::fromTheme("folder-new"), tr("Create Folder"), this);
    actionCollection()->addAction(QStringLiteral("create_folder"), createFolderMenuAction);
    connect(createFolderMenuAction, &QAction::triggered, this, [this]() {
        // Find current folder
        QModelIndex index = openBooksTree->currentIndex();
        QStandardItem* item = index.isValid() ? openBooksModel->itemFromIndex(index) : nullptr;
        if (!item || !item->data(Qt::UserRole + 1).toString().contains("_folder")) {
            // Default to docs folder if no folder selected
            if (openBooksModel && openBooksModel->rowCount() > 0) {
                QStandardItem* book = openBooksModel->item(0);
                for (int i = 0; i < book->rowCount(); ++i) {
                    if (book->child(i)->data(Qt::UserRole + 1).toString() == "docs_folder") {
                        item = book->child(i);
                        break;
                    }
                }
            }
        }
        if (item && currentDb) {
            QString type = item->data(Qt::UserRole + 1).toString();
            bool ok;
            QString name = QInputDialog::getText(this, "Create Folder", "Enter folder name:", QLineEdit::Normal,
                                                 "New Folder", &ok);
            if (ok && !name.isEmpty()) {
                int parentId = item->data(Qt::UserRole).toInt();
                QString fType = "documents";
                if (type == "templates_folder")
                    fType = "templates";
                else if (type == "drafts_folder")
                    fType = "drafts";
                else if (type == "notes_folder")
                    fType = "notes";

                currentDb->addFolder(parentId, name, fType);
                loadDocumentsAndNotes();
            }
        }
    });

    // Endpoints in toolbar
    QWidget* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);

    QLabel* endpointLabel = new QLabel(tr("Endpoint: "), this);
    toolbar->addWidget(endpointLabel);

    endpointComboBox = new QComboBox(this);
    endpointComboBox->setMinimumWidth(150);
    toolbar->addWidget(endpointComboBox);

    connectionStatusLabel = new QLabel(this);
    connectionStatusLabel->setMargin(4);
    toolbar->addWidget(connectionStatusLabel);
    onConnectionStatusChanged(false);  // Initially disconnected

    connect(endpointComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::onActiveEndpointChanged);
    connect(&ollamaClient, &OllamaClient::connectionStatusChanged, this, &MainWindow::onConnectionStatusChanged);

    connect(newBookAction, &QAction::triggered, this, &MainWindow::onCreateBook);
    connect(bookList, &QListWidget::doubleClicked, this,
            &MainWindow::onBookSelected);  // Open book from list via double click
    connect(sendButton, &QPushButton::clicked, this, &MainWindow::onSendMessage);
    connect(inputField, &ChatInputWidget::returnPressed, this, &MainWindow::onSendMessage);
    connect(chatModel, &QStandardItemModel::itemChanged, this, &MainWindow::onItemChanged);
    connect(chatTree->selectionModel(), &QItemSelectionModel::currentChanged, this, &MainWindow::onChatNodeSelected);

    connect(&ollamaClient, &OllamaClient::modelListUpdated, this, [this](const QStringList& models) {
        m_availableModels = models;
        if (m_availableModels.isEmpty()) {
            m_availableModels.append("llama2");  // fallback
        }
        if (m_selectedModel.isEmpty() && !m_availableModels.isEmpty()) {
            m_selectedModel = m_availableModels.first();
            modelSelectButton->setText(m_selectedModel);
            modelLabel->setText(tr("Model: %1").arg(m_selectedModel));
        }
    });

    connect(modelSelectButton, &QPushButton::clicked, this, [this]() {
        ModelSelectionDialog dlg(m_availableModels, this);
        if (dlg.exec() == QDialog::Accepted) {
            QString selected = dlg.selectedModel();
            if (!selected.isEmpty()) {
                m_selectedModel = selected;
                modelSelectButton->setText(m_selectedModel);
                modelLabel->setText(tr("Model: %1").arg(m_selectedModel));
            }
        }
    });

    connect(&ollamaClient, &OllamaClient::pullProgressUpdated, this, &MainWindow::onPullProgressUpdated);
    connect(&ollamaClient, &OllamaClient::pullFinished, this, &MainWindow::onPullFinished);

    QAction* modelExplorerAction = new QAction(QIcon::fromTheme("server-database"), tr("Model Explorer"), this);
    actionCollection()->addAction(QStringLiteral("model_explorer"), modelExplorerAction);
    connect(modelExplorerAction, &QAction::triggered, this, &MainWindow::showModelExplorer);

    QAction* settingsAction = new QAction(QIcon::fromTheme("configure"), tr("Configure KLlamaBooks..."), this);
    actionCollection()->addAction(QStringLiteral("configure_app"), settingsAction);
    connect(settingsAction, &QAction::triggered, this, &MainWindow::showSettingsDialog);

    QAction* debugInfoAction = new QAction(QIcon::fromTheme("tools-report-bug"), tr("Database Debug Info"), this);
    actionCollection()->addAction(QStringLiteral("debug_info"), debugInfoAction);
    connect(debugInfoAction, &QAction::triggered, this, [this]() {
        if (currentDb) {
            QMessageBox::information(this, "Database Debug Info", currentDb->getDatabaseDebugInfo());
        }
    });

    QAction* quitAction = KStandardAction::quit(qApp, &QCoreApplication::quit, actionCollection());
    actionCollection()->addAction("quit", quitAction);

    QAction* aboutQtAction = new QAction(tr("About &Qt"), this);
    connect(aboutQtAction, &QAction::triggered, qApp, &QApplication::aboutQt);
    actionCollection()->addAction(QStringLiteral("about_qt"), aboutQtAction);

    QAction* createFolderAction = new QAction(QIcon::fromTheme("folder-new"), tr("Create Folder"), this);
    actionCollection()->addAction(QStringLiteral("create_folder"), createFolderAction);
    connect(createFolderAction, &QAction::triggered, this, [this]() {
        QModelIndex idx = openBooksTree->currentIndex();
        if (idx.isValid()) {
            QStandardItem* item = openBooksModel->itemFromIndex(idx);
            showVfsContextMenu(openBooksTree->mapToGlobal(openBooksTree->visualRect(idx).center()));
        }
    });

    setupGUI(Default, ":/kllamabooksui.rc");

    // Status Bar
    statusBar = new QStatusBar(this);
    setStatusBar(statusBar);

    statusLabel = new QLabel(tr("Ready"), this);
    statusBar->addWidget(statusLabel);

    modelLabel = new QLabel(tr("Model: Not Selected"), this);
    statusBar->addPermanentWidget(modelLabel);

    queueStatusBtn = new QToolButton(this);
    queueStatusBtn->setText("Q: 0");
    queueStatusBtn->setToolTip(tr("LLM Request Queue"));
    connect(queueStatusBtn, &QToolButton::clicked, this, &MainWindow::showQueueWindow);
    statusBar->addPermanentWidget(queueStatusBtn);

    notificationBtn = new QToolButton(this);
    notificationBtn->setText("🔔 0");
    notificationBtn->setToolTip(tr("Notifications"));
    notificationBtn->setPopupMode(QToolButton::InstantPopup);
    notificationMenu = new QMenu(this);
    notificationBtn->setMenu(notificationMenu);
    statusBar->addPermanentWidget(notificationBtn);

    updateEndpointsList();
    loadBooks();
}

void MainWindow::updateEndpointsList() {
    endpointComboBox->blockSignals(true);
    endpointComboBox->clear();

    QSettings settings;
    QVariantList connections = settings.value("llmConnections").toList();

    if (connections.isEmpty() && settings.contains("ollamaUrl")) {
        // Fallback for old setting
        endpointComboBox->addItem("Default Ollama", settings.value("ollamaUrl", "http://localhost:11434").toString());
        endpointComboBox->setItemData(0, "", Qt::UserRole + 1);  // Auth Key
    } else {
        for (int i = 0; i < connections.size(); ++i) {
            QVariantMap map = connections[i].toMap();
            endpointComboBox->addItem(map["name"].toString(), map["url"].toString());
            endpointComboBox->setItemData(i, map["authKey"].toString(), Qt::UserRole + 1);
        }
    }

    int lastIdx = settings.value("lastEndpointIndex", 0).toInt();
    if (lastIdx >= 0 && lastIdx < endpointComboBox->count()) {
        endpointComboBox->setCurrentIndex(lastIdx);
    }

    endpointComboBox->blockSignals(false);

    if (endpointComboBox->count() > 0) {
        onActiveEndpointChanged(endpointComboBox->currentIndex());
    }
}

void MainWindow::onActiveEndpointChanged(int index) {
    if (index < 0) return;

    QString url = endpointComboBox->itemData(index, Qt::UserRole).toString();
    QString authKey = endpointComboBox->itemData(index, Qt::UserRole + 1).toString();

    ollamaClient.setBaseUrl(url);
    ollamaClient.setAuthKey(authKey);
    ollamaClient.fetchModels();  // Test connection and fetch

    QSettings settings;
    settings.setValue("lastEndpointIndex", index);
}

void MainWindow::onConnectionStatusChanged(bool isOk) {
    if (isOk) {
        connectionStatusLabel->setText("🟢");
        connectionStatusLabel->setToolTip(tr("Connected"));
    } else {
        connectionStatusLabel->setText("🔴");
        connectionStatusLabel->setToolTip(tr("Disconnected / Error"));
    }
}

void MainWindow::showSettingsDialog() {
    SettingsDialog* dlg = new SettingsDialog(this);
    connect(dlg, &SettingsDialog::settingsApplied, this, [this]() {
        updateEndpointsList();
        updateInputBehavior();
    });
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::showInputSettingsMenu() {
    QMenu menu(this);

    QMenu* bookMenu = menu.addMenu(tr("Book Behavior"));
    QActionGroup* bookGroup = new QActionGroup(&menu);
    QAction* bDef = bookMenu->addAction(tr("Use Global Default"));
    bDef->setData("default");
    QAction* bEnter = bookMenu->addAction(tr("Enter to Send"));
    bEnter->setData("EnterToSend");
    QAction* bCtrl = bookMenu->addAction(tr("Ctrl+Enter to Send"));
    bCtrl->setData("CtrlEnterToSend");
    bDef->setCheckable(true);
    bEnter->setCheckable(true);
    bCtrl->setCheckable(true);
    bookGroup->addAction(bDef);
    bookGroup->addAction(bEnter);
    bookGroup->addAction(bCtrl);

    QString currentBookSetting = "default";
    if (currentDb && currentDb->isOpen()) {
        currentBookSetting = currentDb->getSetting("book", 0, "sendBehavior", "default");
    } else {
        bookMenu->setEnabled(false);
    }
    if (currentBookSetting == "EnterToSend")
        bEnter->setChecked(true);
    else if (currentBookSetting == "CtrlEnterToSend")
        bCtrl->setChecked(true);
    else
        bDef->setChecked(true);

    connect(bookGroup, &QActionGroup::triggered, this, [this](QAction* action) {
        if (currentDb && currentDb->isOpen()) {
            currentDb->setSetting("book", 0, "sendBehavior", action->data().toString());
            updateInputBehavior();
        }
    });

    QMenu* chatMenu = menu.addMenu(tr("Chat Behavior"));
    QActionGroup* chatGroup = new QActionGroup(&menu);
    QAction* cDef = chatMenu->addAction(tr("Use Book Default"));
    cDef->setData("default");
    QAction* cEnter = chatMenu->addAction(tr("Enter to Send"));
    cEnter->setData("EnterToSend");
    QAction* cCtrl = chatMenu->addAction(tr("Ctrl+Enter to Send"));
    cCtrl->setData("CtrlEnterToSend");
    cDef->setCheckable(true);
    cEnter->setCheckable(true);
    cCtrl->setCheckable(true);
    chatGroup->addAction(cDef);
    chatGroup->addAction(cEnter);
    chatGroup->addAction(cCtrl);

    QString currentChatSetting = "default";

    // Determine the root ID of the current chat path
    int rootId = 0;
    if (currentDb && currentDb->isOpen() && currentLastNodeId != 0) {
        QList<MessageNode> msgs = currentDb->getMessages();
        QList<MessageNode> path;
        getPathToRoot(currentLastNodeId, msgs, path);
        if (!path.isEmpty()) {
            rootId = path.first().id;  // The root node of this chat
        }
    }

    if (currentDb && currentDb->isOpen() && rootId != 0) {
        currentChatSetting = currentDb->getSetting("chat", rootId, "sendBehavior", "default");
    } else if (!currentDb || !currentDb->isOpen()) {
        chatMenu->setEnabled(false);
    }
    if (currentChatSetting == "EnterToSend")
        cEnter->setChecked(true);
    else if (currentChatSetting == "CtrlEnterToSend")
        cCtrl->setChecked(true);
    else
        cDef->setChecked(true);

    connect(chatGroup, &QActionGroup::triggered, this, [this, rootId](QAction* action) {
        if (currentDb && currentDb->isOpen()) {
            currentDb->setSetting("chat", rootId, "sendBehavior", action->data().toString());
            updateInputBehavior();
        }
    });

    menu.addSeparator();

    QAction* saveDraftAction = menu.addAction(tr("Save as Draft"));
    connect(saveDraftAction, &QAction::triggered, this, [this]() {
        if (!currentDb) return;
        QString content = chatTextArea->toPlainText();
        bool ok;
        QString title =
            QInputDialog::getText(this, "Save Draft", "Name this draft:", QLineEdit::Normal, "New Draft", &ok);
        if (ok && !title.isEmpty()) {
            currentDb->addDraft(0, title, content);
            statusBar->showMessage(tr("Draft saved."), 3000);
            loadSession(0);
        }
    });

    QAction* saveTemplateAction = menu.addAction(tr("Save as Template"));
    connect(saveTemplateAction, &QAction::triggered, this, [this]() {
        if (!currentDb) return;
        QString content = chatTextArea->toPlainText();
        bool ok;
        QString title =
            QInputDialog::getText(this, "Save Template", "Name this template:", QLineEdit::Normal, "New Template", &ok);
        if (ok && !title.isEmpty()) {
            currentDb->addTemplate(0, title, content);
            statusBar->showMessage(tr("Template saved."), 3000);
            loadDocumentsAndNotes();
        }
    });

    menu.addSeparator();
    QAction* loadTemplateAction = menu.addAction(tr("Load Template..."));
    connect(loadTemplateAction, &QAction::triggered, this, [this]() {
        if (currentDb) {
            QList<DocumentNode> templates = currentDb->getTemplates();
            if (templates.isEmpty()) {
                statusBar->showMessage("No templates found.", 3000);
            } else {
                QStringList titles;
                for (const auto& t : templates) titles << t.title;
                bool ok;
                QString sel = QInputDialog::getItem(this, "Load Template", "Choose Template:", titles, 0, false, &ok);
                if (ok) {
                    for (const auto& t : templates) {
                        if (t.title == sel) {
                            inputField->setPlainText(t.content);
                            break;
                        }
                    }
                }
            }
        }
    });

    QAction* resumeDraftAction = menu.addAction(tr("Resume Draft..."));
    connect(resumeDraftAction, &QAction::triggered, this, [this]() {
        if (currentDb) {
            QList<DocumentNode> drafts = currentDb->getDrafts();
            if (drafts.isEmpty()) {
                statusBar->showMessage("No drafts found.", 3000);
            } else {
                QStringList titles;
                for (const auto& d : drafts) titles << d.title;
                bool ok;
                QString sel = QInputDialog::getItem(this, "Resume Draft", "Choose Draft:", titles, 0, false, &ok);
                if (ok) {
                    for (const auto& d : drafts) {
                        if (d.title == sel) {
                            inputField->setPlainText(d.content);
                            break;
                        }
                    }
                }
            }
        }
    });

    menu.addSeparator();
    QAction* globalSettingsAction = menu.addAction(tr("Global Settings..."));
    connect(globalSettingsAction, &QAction::triggered, this, &MainWindow::showSettingsDialog);

    menu.exec(inputSettingsButton->mapToGlobal(QPoint(0, inputSettingsButton->height())));
}

void MainWindow::updateInputBehavior() {
    QString behaviorStr = "EnterToSend";  // Ultimate default

    QSettings settings;
    QString globalSetting = settings.value("globalSendBehavior", "EnterToSend").toString();

    QString bookSetting = "default";
    QString chatSetting = "default";

    if (currentDb && currentDb->isOpen()) {
        bookSetting = currentDb->getSetting("book", 0, "sendBehavior", "default");
        int rootId = 0;
        if (currentLastNodeId != 0) {
            QList<MessageNode> msgs = currentDb->getMessages();
            QList<MessageNode> path;
            getPathToRoot(currentLastNodeId, msgs, path);
            if (!path.isEmpty()) {
                rootId = path.first().id;
            }
        }
        if (rootId != 0) {
            chatSetting = currentDb->getSetting("chat", rootId, "sendBehavior", "default");
        }
    }

    if (chatSetting != "default") {
        behaviorStr = chatSetting;
    } else if (bookSetting != "default") {
        behaviorStr = bookSetting;
    } else {
        behaviorStr = globalSetting;
    }

    if (behaviorStr == "CtrlEnterToSend") {
        inputField->setSendBehavior(ChatInputWidget::CtrlEnterToSend);
    } else {
        inputField->setSendBehavior(ChatInputWidget::EnterToSend);
    }
}

void MainWindow::setupWindow() {
    setWindowTitle("KLlamaBooks");
    resize(800, 600);

    QSettings settings;
    if (settings.contains("geometry")) {
        restoreGeometry(settings.value("geometry").toByteArray());
    }
    if (settings.contains("splitterState")) {
        splitter->restoreState(settings.value("splitterState").toByteArray());
    }
    if (settings.contains("leftSplitterState")) {
        leftSplitter->restoreState(settings.value("leftSplitterState").toByteArray());
    }

    // Defer loading open books slightly so everything is initialized, or do it directly if safe
    QStringList openBooks = settings.value("openBooks").toStringList();
    for (const QString& book : openBooks) {
        handleBookDrop(book);
    }
}

void MainWindow::loadBooks() {
    bookList->clear();
    QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    if (!dir.exists()) dir.mkpath(".");

    QSettings settings;
    QStringList favorites = settings.value("favorites").toStringList();

    QStringList filters;
    filters << "*.db";
    // Sort by last modified (accessed/changed)
    QFileInfoList fileList =
        dir.entryInfoList(filters, QDir::Files | QDir::NoDotAndDotDot, QDir::Time | QDir::Reversed);

    // Separate into favorites and others
    QList<QFileInfo> favList;
    QList<QFileInfo> otherList;

    for (const QFileInfo& file : fileList) {
        if (favorites.contains(file.fileName())) {
            favList.append(file);
        } else {
            otherList.append(file);
        }
    }

    // Sort within lists
    auto sortByTimeDesc = [](const QFileInfo& a, const QFileInfo& b) { return a.lastModified() > b.lastModified(); };

    std::sort(favList.begin(), favList.end(), sortByTimeDesc);
    std::sort(otherList.begin(), otherList.end(), sortByTimeDesc);

    // Add favorites first
    for (const QFileInfo& file : favList) {
        if (m_openDatabases.contains(file.fileName())) continue;
        QListWidgetItem* item = new QListWidgetItem(QIcon::fromTheme("emblem-favorite"), file.fileName());
        item->setToolTip(tr("Last modified: %1").arg(QLocale().toString(file.lastModified(), QLocale::ShortFormat)));
        bookList->addItem(item);
    }

    // Then add non-favorites
    for (const QFileInfo& file : otherList) {
        if (m_openDatabases.contains(file.fileName())) continue;
        QListWidgetItem* item = new QListWidgetItem(file.fileName());
        item->setToolTip(tr("Last modified: %1").arg(QLocale().toString(file.lastModified(), QLocale::ShortFormat)));
        bookList->addItem(item);
    }
}

void MainWindow::showModelExplorer() {
    ModelExplorer* explorer = new ModelExplorer(&ollamaClient, this);
    explorer->setAttribute(Qt::WA_DeleteOnClose);
    explorer->show();
}

void MainWindow::onOpenBook() {
    QString startPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString filePath = QFileDialog::getOpenFileName(this, tr("Open Book"), startPath, tr("Books (*.db)"));
    if (!filePath.isEmpty()) {
        QFileInfo fileInfo(filePath);
        handleBookDrop(fileInfo.fileName());
    }
}

void MainWindow::onCloseBook() {
    if (currentDb && currentDb->isOpen()) {
        QFileInfo fileInfo(currentDb->filepath());
        closeBook(fileInfo.fileName());
    }
}

void MainWindow::onOpenBookLocation() {
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::onCreateBook() {
    bool ok;
    QString bookName = QInputDialog::getText(this, "New Book", "Enter book name:", QLineEdit::Normal, "", &ok);
    if (ok && !bookName.isEmpty()) {
        QString password =
            QInputDialog::getText(this, "Password", "Enter password (optional):", QLineEdit::Password, "", &ok);
        if (ok) {
            bool savePassword = false;
            if (!password.isEmpty()) {
                QMessageBox::StandardButton reply =
                    QMessageBox::question(this, "Save Password", "Do you want to save this password to KWallet?",
                                          QMessageBox::Yes | QMessageBox::No);
                if (reply == QMessageBox::Yes) {
                    savePassword = true;
                }
            }
            QString fileName = bookName + ".db";
            if (savePassword) {
                WalletManager::savePassword(fileName, password);
            }

            QString filePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/" + fileName;
            BookDatabase newDb(filePath);
            newDb.open(password);  // Creates the schema

            loadBooks();
        }
    }
}

void MainWindow::showVfsContextMenu(const QPoint& pos) {
    QModelIndex index = vfsExplorer->indexAt(pos);
    QStandardItem* item = nullptr;
    QMenu menu(this);

    QAction* newChatAction = nullptr;
    QAction* newDocAction = nullptr;
    QAction* newNoteAction = nullptr;
    QAction* createFolderAction = nullptr;
    QAction* copyAction = nullptr;
    QAction* pasteAction = nullptr;
    QAction* forkAction = nullptr;

    QModelIndex treeIndex = openBooksTree->currentIndex();
    QStandardItem* treeItem = nullptr;
    if (treeIndex.isValid() && openBooksModel) {
        treeItem = openBooksModel->itemFromIndex(treeIndex);
    }
    QString currentFolderType = treeItem ? treeItem->data(Qt::UserRole + 1).toString() : "";

    if (currentFolderType == "chats_folder" || currentFolderType == "chat_session") {
        newChatAction = menu.addAction(QIcon::fromTheme("chat-message-new"), "New Chat");
        menu.addSeparator();
    } else if (currentFolderType == "docs_folder" || currentFolderType == "templates_folder" ||
               currentFolderType == "drafts_folder" || currentFolderType == "notes_folder") {
        QString actionName = "New Document";
        if (currentFolderType == "templates_folder")
            actionName = "New Template";
        else if (currentFolderType == "drafts_folder")
            actionName = "New Draft";
        else if (currentFolderType == "notes_folder")
            actionName = "New Note";

        if (currentFolderType == "notes_folder") {
            newNoteAction = menu.addAction(QIcon::fromTheme("document-new"), actionName);
        } else {
            newDocAction = menu.addAction(QIcon::fromTheme("document-new"), actionName);
        }
        createFolderAction = menu.addAction(QIcon::fromTheme("folder-new"), "Create Folder");
        menu.addSeparator();
    }

    if (index.isValid()) {
        item = vfsModel->itemFromIndex(index);
        if (item && item->text() != "..") {
            QString itemType = item->data(Qt::UserRole + 1).toString();
            if (itemType == "template") {
                QAction* loadTemplateAction = menu.addAction(QIcon::fromTheme("document-import"), "Load Template");
                QAction* sel = menu.exec(vfsExplorer->viewport()->mapToGlobal(pos));
                if (sel == loadTemplateAction) {
                    int docId = item->data(Qt::UserRole).toInt();
                    if (currentDb) {
                        QList<DocumentNode> templates = currentDb->getTemplates();
                        for (const auto& t : templates) {
                            if (t.id == docId) {
                                inputField->setPlainText(t.content);
                                break;
                            }
                        }
                    }
                }
                return;
            }
            if (itemType == "draft") {
                QAction* resumeDraftAction = menu.addAction(QIcon::fromTheme("document-import"), "Resume Draft");
                QAction* sel = menu.exec(vfsExplorer->viewport()->mapToGlobal(pos));
                if (sel == resumeDraftAction) {
                    int docId = item->data(Qt::UserRole).toInt();
                    if (currentDb) {
                        QList<DocumentNode> drafts = currentDb->getDrafts();
                        for (const auto& d : drafts) {
                            if (d.id == docId) {
                                inputField->setPlainText(d.content);
                                break;
                            }
                        }
                    }
                }
                return;
            }
            if (itemType.isEmpty() || itemType == "chat_session") {
                copyAction = menu.addAction(QIcon::fromTheme("edit-copy"), "Copy Message");
                pasteAction = menu.addAction(QIcon::fromTheme("edit-paste"), "Paste to Input");
                menu.addSeparator();
                forkAction = menu.addAction(QIcon::fromTheme("call-start"), "Fork from Here");
                menu.addSeparator();
                menu.addAction(QIcon::fromTheme("document-export"), "Export Chat Session");
            }
        }
    } else {
        // Empty space - already populated with folder actions at top
    }

    QAction* selectedAction = menu.exec(vfsExplorer->viewport()->mapToGlobal(pos));

    if (newChatAction && selectedAction == newChatAction) {
        if (treeItem) addPhantomItem(treeItem, currentFolderType);
    } else if (newDocAction && selectedAction == newDocAction) {
        if (treeItem) addPhantomItem(treeItem, currentFolderType);
    } else if (selectedAction == createFolderAction) {
        bool ok;
        QString name =
            QInputDialog::getText(this, "Create Folder", "Enter folder name:", QLineEdit::Normal, "New Folder", &ok);
        if (ok && !name.isEmpty() && currentDb) {
            int parentId = treeItem ? treeItem->data(Qt::UserRole).toInt() : 0;
            QString fType = "documents";
            if (currentFolderType == "templates_folder")
                fType = "templates";
            else if (currentFolderType == "drafts_folder")
                fType = "drafts";
            else if (currentFolderType == "notes_folder")
                fType = "notes";
            else if (currentFolderType == "chats_folder")
                fType = "chats";

            currentDb->addFolder(parentId, name, fType);
            loadDocumentsAndNotes();
        }
    } else if (newNoteAction && selectedAction == newNoteAction) {
        if (treeItem) addPhantomItem(treeItem, currentFolderType);
    } else if (item && selectedAction == copyAction) {
        QString fullText = item->text();
        int bracketIndex = fullText.indexOf("] ");
        if (bracketIndex != -1) fullText = fullText.mid(bracketIndex + 2);
        QGuiApplication::clipboard()->setText(fullText);
    } else if (item && selectedAction == pasteAction) {
        if (inputModeStack->currentIndex() == 0) {
            inputField->setPlainText(inputField->toPlainText() + QGuiApplication::clipboard()->text());
        } else {
            multiLineInput->insertPlainText(QGuiApplication::clipboard()->text());
        }
    } else if (item && selectedAction == forkAction) {
        currentLastNodeId = item->data(Qt::UserRole).toInt();
        if (currentDb) {
            updateLinearChatView(currentLastNodeId, currentDb->getMessages());
            mainContentStack->setCurrentWidget(chatWindowView);
            chatTextArea->setFocus();
        }
    }
}

void MainWindow::onBookSelected(const QModelIndex& index) {
    QString fileName = bookList->item(index.row())->text();
    QString filePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/" + fileName;

    QString password = WalletManager::loadPassword(fileName);

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
    } else {
        auto db = std::make_shared<BookDatabase>(filePath);
        if (!db->open(password)) {
            bool ok;
            password = QInputDialog::getText(this, "Unlock Book", "Enter password for " + fileName + ":",
                                             QLineEdit::Password, "", &ok);
            if (ok && db->open(password)) {
                if (!password.isEmpty()) {
                    QMessageBox::StandardButton reply =
                        QMessageBox::question(this, "Save Password", "Do you want to save this password to KWallet?",
                                              QMessageBox::Yes | QMessageBox::No);
                    if (reply == QMessageBox::Yes) {
                        WalletManager::savePassword(fileName, password);
                    }
                }
            } else {
                QMessageBox::warning(this, "Error", "Could not open book.");
                return;
            }
        }
        m_openDatabases[fileName] = db;
        currentDb = db;
        QueueManager::instance().addDatabase(db);
    }

    QueueManager::instance().setActiveDatabase(currentDb);

    // Add to open books tree
    QStandardItem* bookItem = new QStandardItem(QIcon::fromTheme("application-x-sqlite3"), fileName);
    bookItem->setData("book", Qt::UserRole + 1);    // Type
    bookItem->setData(filePath, Qt::UserRole + 2);  // Store path
    QStandardItem* chatsItem = new QStandardItem(QIcon::fromTheme("folder-open"), "Chats");
    chatsItem->setData("chats_folder", Qt::UserRole + 1);

    QStandardItem* docsItem = new QStandardItem(QIcon::fromTheme("folder-open"), "Documents");
    docsItem->setData("docs_folder", Qt::UserRole + 1);
    QStandardItem* notesItem = new QStandardItem(QIcon::fromTheme("folder-open"), "Notes");
    notesItem->setData("notes_folder", Qt::UserRole + 1);
    QStandardItem* templatesItem = new QStandardItem(QIcon::fromTheme("folder-open"), "Templates");
    templatesItem->setData("templates_folder", Qt::UserRole + 1);
    QStandardItem* draftsItem = new QStandardItem(QIcon::fromTheme("folder-open"), "Drafts");
    draftsItem->setData("drafts_folder", Qt::UserRole + 1);

    // Populate actual chats from database directly into the tree
    auto dbPtr = currentDb;  // Keep shared_ptr to avoid issues during iteration
    QList<MessageNode> msgs = dbPtr->getMessages();
    populateChatFolders(chatsItem, 0, msgs, dbPtr.get());

    populateDocumentFolders(docsItem, 0, "documents", dbPtr.get());
    populateDocumentFolders(templatesItem, 0, "templates", dbPtr.get());
    populateDocumentFolders(draftsItem, 0, "drafts", dbPtr.get());
    populateDocumentFolders(notesItem, 0, "notes", dbPtr.get());

    bookItem->appendRow(chatsItem);
    bookItem->appendRow(docsItem);
    bookItem->appendRow(notesItem);
    bookItem->appendRow(templatesItem);
    bookItem->appendRow(draftsItem);

    openBooksModel->appendRow(bookItem);
    openBooksTree->expandAll();

    // Remove from closed books list
    delete bookList->takeItem(index.row());

    loadSession(0);  // For now, load all as one session

    openBooksTree->setCurrentIndex(bookItem->index());
}

void MainWindow::populateDocumentFolders(QStandardItem* parentItem, int folderId, const QString& type,
                                         BookDatabase* db) {
    if (!db) return;

    // First, add subfolders
    QList<FolderNode> folders = db->getFolders(type);
    for (const auto& folder : folders) {
        if (folder.parentId == folderId) {
            QStandardItem* item = new QStandardItem(QIcon::fromTheme("folder-open"), folder.name);
            item->setData(folder.id, Qt::UserRole);
            QString folderTypeSuffix = "_folder";
            if (type == "documents")
                item->setData("docs_folder", Qt::UserRole + 1);
            else if (type == "templates")
                item->setData("templates_folder", Qt::UserRole + 1);
            else if (type == "drafts")
                item->setData("drafts_folder", Qt::UserRole + 1);
            else if (type == "notes")
                item->setData("notes_folder", Qt::UserRole + 1);

            parentItem->appendRow(item);
            populateDocumentFolders(item, folder.id, type, db);
        }
    }

    // Then, add items in this folder
    if (type == "documents" || type == "templates" || type == "drafts") {
        QList<DocumentNode> docs;
        if (type == "documents")
            docs = db->getDocuments(folderId);
        else if (type == "templates")
            docs = db->getTemplates(folderId);
        else if (type == "drafts")
            docs = db->getDrafts(folderId);

        for (const auto& doc : docs) {
            QString itemType = (type == "documents") ? "document" : ((type == "templates") ? "template" : "draft");
            QStandardItem* item = new QStandardItem(QIcon::fromTheme("text-x-generic"), doc.title);
            item->setData(doc.id, Qt::UserRole);
            item->setData(itemType, Qt::UserRole + 1);
            parentItem->appendRow(item);
        }
    } else if (type == "notes") {
        QList<NoteNode> notes = db->getNotes(folderId);
        for (const auto& note : notes) {
            QStandardItem* item = new QStandardItem(QIcon::fromTheme("text-x-generic"), note.title);
            item->setData(note.id, Qt::UserRole);
            item->setData("note", Qt::UserRole + 1);
            parentItem->appendRow(item);
        }
    }
}

void MainWindow::addPhantomItem(QStandardItem* folderItem, const QString& type) {
    if (!folderItem && openBooksModel && openBooksModel->rowCount() > 0) {
        QStandardItem* book = openBooksModel->item(0);
        for (int i = 0; i < book->rowCount(); ++i) {
            if (book->child(i)->data(Qt::UserRole + 1).toString() == type) {
                folderItem = book->child(i);
                break;
            }
        }
    }
    if (!folderItem) return;

    QStandardItem* phantomItem = new QStandardItem(QIcon::fromTheme("text-x-generic"), "*New Item*");
    QFont f = phantomItem->font();
    f.setItalic(true);
    phantomItem->setFont(f);
    phantomItem->setData(0, Qt::UserRole);

    QString itemType = type;
    if (type == "chats_folder")
        itemType = "chat_session";
    else if (type == "docs_folder")
        itemType = "document";
    else if (type == "templates_folder")
        itemType = "template";
    else if (type == "drafts_folder")
        itemType = "draft";
    else if (type == "notes_folder")
        itemType = "note";

    phantomItem->setData(itemType, Qt::UserRole + 1);
    folderItem->appendRow(phantomItem);

    openBooksTree->setExpanded(folderItem->index(), true);
    openBooksTree->setCurrentIndex(phantomItem->index());
}

void MainWindow::loadSession(int rootId) {
    chatModel->clear();
    chatTextArea->clear();
    if (!currentDb) return;

    loadDocumentsAndNotes();

    QList<MessageNode> msgs = currentDb->getMessages();
    QStandardItem* rootItem = chatModel->invisibleRootItem();
    populateChatFolders(rootItem, 0, msgs, currentDb.get());
    chatTree->expandAll();

    if (!msgs.isEmpty()) {
        currentLastNodeId = msgs.last().id;
    } else {
        currentLastNodeId = 0;
    }

    updateLinearChatView(currentLastNodeId, msgs);
    updateInputBehavior();
}

void MainWindow::getPathToRoot(int nodeId, const QList<MessageNode>& allMessages, QList<MessageNode>& path) {
    for (const auto& msg : allMessages) {
        if (msg.id == nodeId) {
            path.prepend(msg);  // Prepend to build path from root downwards
            if (msg.parentId != 0) {
                getPathToRoot(msg.parentId, allMessages, path);
            }
            break;
        }
    }
}

int MainWindow::getEndOfLinearPath(int startId, const QList<MessageNode>& allMessages,
                                   QList<MessageNode>& outChildren) {
    int currentId = startId;
    while (true) {
        outChildren.clear();
        for (const auto& msg : allMessages) {
            if (msg.parentId == currentId) {
                outChildren.append(msg);
            }
        }
        if (outChildren.size() == 1) {
            currentId = outChildren.first().id;
        } else {
            break;
        }
    }
    return currentId;
}

void MainWindow::populateChatFolders(QStandardItem* parentItem, int folderId, const QList<MessageNode>& allMessages,
                                     BookDatabase* db) {
    if (!db) return;

    // 1. Add subfolders of type 'chats'
    QList<FolderNode> folders = db->getFolders("chats");
    for (const auto& folder : folders) {
        if (folder.parentId == folderId) {
            QStandardItem* folderItem = new QStandardItem(QIcon::fromTheme("folder-open"), folder.name);
            folderItem->setData(folder.id, Qt::UserRole);
            folderItem->setData("chats_folder", Qt::UserRole + 1);
            parentItem->appendRow(folderItem);

            // Recurse into subfolders
            populateChatFolders(folderItem, folder.id, allMessages, db);
        }
    }

    // 2. Add root messages (sessions) that belong to this folder
    for (const auto& msg : allMessages) {
        if (msg.parentId == 0 && msg.folderId == folderId) {
            // Determine children to show the correct icon/label
            QList<MessageNode> children;
            int endNodeId = getEndOfLinearPath(msg.id, allMessages, children);

            QString displayTitle = msg.content.simplified();
            if (displayTitle.length() > 30) {
                displayTitle = displayTitle.left(27) + "...";
            }
            if (displayTitle.isEmpty()) {
                displayTitle = "New Chat";
            }

            QStandardItem* item = nullptr;
            if (children.size() > 1) {
                item = new QStandardItem(QIcon::fromTheme("vcs-branch", QIcon::fromTheme("folder-open")), displayTitle);
            } else {
                item = new QStandardItem(QIcon::fromTheme("text-x-generic"), displayTitle);
            }

            item->setData(endNodeId, Qt::UserRole);
            item->setData("chat_node", Qt::UserRole + 1);
            parentItem->appendRow(item);

            // Populate the rest of the branch under this root message
            populateMessageForks(item, endNodeId, allMessages);
        }
    }
}

void MainWindow::populateMessageForks(QStandardItem* parentItem, int parentId, const QList<MessageNode>& allMessages) {
    for (const auto& msg : allMessages) {
        if (msg.parentId == parentId) {
            // Find all children of this message
            QList<MessageNode> children;
            int endNodeId = getEndOfLinearPath(msg.id, allMessages, children);

            QString displayTitle = msg.content.simplified();
            if (displayTitle.length() > 30) {
                displayTitle = displayTitle.left(27) + "...";
            }
            if (displayTitle.isEmpty()) {
                displayTitle = "New Chat";
            }

            QStandardItem* item = nullptr;
            if (children.size() > 1) {
                item = new QStandardItem(QIcon::fromTheme("vcs-branch", QIcon::fromTheme("folder-open")), displayTitle);
            } else {
                item = new QStandardItem(QIcon::fromTheme("text-x-generic"), displayTitle);
            }

            item->setData(endNodeId, Qt::UserRole);
            item->setData("chat_node", Qt::UserRole + 1);
            parentItem->appendRow(item);
            // Recursively populate under this item.
            populateMessageForks(item, endNodeId, allMessages);
        }
    }
}

void MainWindow::updateLinearChatView(int tailNodeId, const QList<MessageNode>& allMessages) {
    if (currentDb) {
        currentDb->dismissNotificationByMessageId(tailNodeId);
        updateNotificationStatus();
    }
    chatTextArea->blockSignals(true);
    chatTextArea->clear();
    currentChatPath.clear();

    QStandardItem* foundFolderItem = nullptr;
    if (openBooksTree && openBooksModel) {
        for (int i = 0; i < openBooksModel->rowCount(); ++i) {
            QStandardItem* bookItem = openBooksModel->item(i);
            for (int j = 0; j < bookItem->rowCount(); ++j) {
                QStandardItem* folderItem = bookItem->child(j);
                if (folderItem && folderItem->data(Qt::UserRole + 1).toString() == "chats_folder") {
                    if (tailNodeId == 0) {
                        foundFolderItem = folderItem;
                        break;
                    } else {
                        foundFolderItem = findItem(folderItem, tailNodeId);
                        if (foundFolderItem) break;
                    }
                }
            }
            if (foundFolderItem) break;
        }

        if (foundFolderItem) {
            openBooksTree->selectionModel()->blockSignals(true);
            openBooksTree->selectionModel()->select(foundFolderItem->index(),
                                                    QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Current);
            openBooksTree->scrollTo(foundFolderItem->index());
            openBooksTree->selectionModel()->blockSignals(false);
        }
    }

    if (forkExplorerModel && chatForkExplorer && foundFolderItem) {
        forkExplorerModel->clear();

        bool hasChildren = foundFolderItem->rowCount() > 0;

        if (isCreatingNewChat) {
            chatForkExplorer->hide();
            if (chatInputContainer) chatInputContainer->show();
        } else if (!hasChildren && tailNodeId != 0) {
            chatForkExplorer->hide();
            if (chatInputContainer) chatInputContainer->show();
        } else {
            chatForkExplorer->show();

            if (tailNodeId == 0) {
                if (chatInputContainer) chatInputContainer->hide();
            } else {
                if (chatInputContainer) chatInputContainer->show();
            }

            if (tailNodeId != 0) {
                QStandardItem* parentInTree = foundFolderItem->parent();
                int parentId = 0;
                if (parentInTree && parentInTree->data(Qt::UserRole + 1).toString() != "chats_folder") {
                    parentId = parentInTree->data(Qt::UserRole).toInt();
                }
                QStandardItem* upItem = new QStandardItem(QIcon::fromTheme("go-up"), "..");
                upItem->setData(parentId, Qt::UserRole);
                forkExplorerModel->appendRow(upItem);
            }

            for (int i = 0; i < foundFolderItem->rowCount(); ++i) {
                QStandardItem* childTreeItem = foundFolderItem->child(i);
                QStandardItem* childItem = new QStandardItem(childTreeItem->icon(), childTreeItem->text());
                childItem->setData(childTreeItem->data(Qt::UserRole), Qt::UserRole);
                forkExplorerModel->appendRow(childItem);
            }
        }
    } else if (forkExplorerModel && chatForkExplorer) {
        chatForkExplorer->hide();
        if (chatInputContainer) chatInputContainer->hide();
    }

    if (tailNodeId == 0 || allMessages.isEmpty()) {
        return;
    }

    getPathToRoot(tailNodeId, allMessages, currentChatPath);

    QString userName = currentDb ? currentDb->getSetting("book", 0, "userName", "User") : "User";
    QString assistantName = currentDb ? currentDb->getSetting("book", 0, "assistantName", "Assistant") : "Assistant";

    for (const auto& node : currentChatPath) {
        QString roleName = (node.role == "user") ? userName : assistantName;

        QTextCursor cursor(chatTextArea->document());
        cursor.movePosition(QTextCursor::End);
        chatTextArea->setTextCursor(cursor);

        chatTextArea->insertHtml(QString("<div style='font-weight: bold;'>[%1]</div>").arg(roleName.toHtmlEscaped()));
        chatTextArea->insertPlainText(node.content + "\n\n");
    }

    chatTextArea->blockSignals(false);
    if (discardChangesBtn) discardChangesBtn->hide();
    updateBreadcrumbs();
}

void MainWindow::updateBreadcrumbs() {
    if (!breadcrumbLayout) return;

    QLayoutItem* item;
    while ((item = breadcrumbLayout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }

    if (!currentDb) {
        breadcrumbWidget->hide();
        return;
    }

    breadcrumbWidget->show();

    QModelIndex index = openBooksTree->currentIndex();
    if (!index.isValid()) {
        breadcrumbLayout->addWidget(new QLabel("No selection", this));
        breadcrumbLayout->addStretch();
        return;
    }

    QStandardItem* treeItem = openBooksModel->itemFromIndex(index);
    if (!treeItem) return;

    QString type = treeItem->data(Qt::UserRole + 1).toString();
    QStringList parts;
    if (type == "book") {
        parts << treeItem->text();
    } else {
        parts << openBooksModel->item(0)->text();  // Book name
        if (type.contains("folder")) {
            parts << treeItem->text();
        } else {
            parts << treeItem->parent()->text() << treeItem->text();
        }
    }

    for (int i = 0; i < parts.size(); ++i) {
        if (i < parts.size() - 1) {
            QLabel* label = new QLabel(parts[i], this);
            breadcrumbLayout->addWidget(label);
            breadcrumbLayout->addWidget(new QLabel(">", this));
        } else {
            QLineEdit* edit = new QLineEdit(parts[i], this);
            edit->setFrame(false);
            edit->setStyleSheet("QLineEdit { background: transparent; }");
            QFont f = edit->font();
            f.setBold(true);
            edit->setFont(f);

            edit->setProperty("is_breadcrumb_edit", true);
            edit->installEventFilter(this);

            int id = treeItem->data(Qt::UserRole).toInt();
            if (id == 0) {
                edit->setReadOnly(false);
                edit->setFocus();
                edit->selectAll();
            } else {
                edit->setReadOnly(true);
            }

            breadcrumbLayout->addWidget(edit);

            connect(edit, &QLineEdit::returnPressed, this, [this, edit, treeItem, type]() {
                QString newName = edit->text().trimmed();
                if (newName.isEmpty() || newName == treeItem->text()) {
                    edit->setText(treeItem->text());
                    edit->clearFocus();
                    return;
                }

                int id = treeItem->data(Qt::UserRole).toInt();

                if (id != 0) {
                    if (type == "document")
                        currentDb->updateDocument(id, newName, documentEditorView->toPlainText());
                    else if (type == "template")
                        currentDb->updateTemplate(id, newName, documentEditorView->toPlainText());
                    else if (type == "draft")
                        currentDb->updateDraft(id, newName, documentEditorView->toPlainText());
                    else if (type == "note")
                        currentDb->updateNote(id, newName, noteEditorView->toPlainText());
                    statusBar->showMessage(tr("Renamed to %1").arg(newName), 3000);
                }

                treeItem->setText(newName);
                edit->clearFocus();
            });

            connect(edit, &QLineEdit::editingFinished, this, [edit, treeItem]() {
                QString newName = edit->text().trimmed();
                if (!newName.isEmpty()) {
                    treeItem->setText(newName);
                } else {
                    edit->setText(treeItem->text());
                }
                edit->setReadOnly(true);
                edit->setSelection(0, 0);
            });
        }
    }
    breadcrumbLayout->addStretch();
}

void MainWindow::onBreadcrumbClicked(const QString& type, int id) {
    // Allows jumping up the tree
}

void MainWindow::onRenameCurrentItem() {
    // Enables inline rename
}

void MainWindow::onDiscardChanges() {
    if (!currentDb || currentLastNodeId == 0) return;
    updateLinearChatView(currentLastNodeId, currentDb->getMessages());
    discardChangesBtn->hide();
    statusBar->showMessage(tr("Changes discarded."), 3000);
}

void MainWindow::populateTree(QStandardItem* parentItem, int parentId, const QList<MessageNode>& allMessages) {
    for (const auto& msg : allMessages) {
        if (msg.parentId == parentId) {
            QStandardItem* item = new QStandardItem(QString("[%1] %2").arg(msg.role, msg.content));
            item->setData(msg.id, Qt::UserRole);
            parentItem->appendRow(item);
            populateTree(item, msg.id, allMessages);
        }
    }
}

QStandardItem* MainWindow::findItem(QStandardItem* parent, int id) {
    for (int i = 0; i < parent->rowCount(); ++i) {
        QStandardItem* child = parent->child(i);
        if (child->data(Qt::UserRole).toInt() == id) {
            return child;
        }
        QStandardItem* found = findItem(child, id);
        if (found) return found;
    }
    return nullptr;
}

void MainWindow::onSendMessage() {
    if (!currentDb) return;

    QString text;
    if (inputModeStack->currentIndex() == 0) {
        text = inputField->toPlainText().trimmed();
        if (text.isEmpty()) return;
        inputField->clear();
    } else {
        text = multiLineInput->toPlainText();
        if (text.isEmpty()) return;
        multiLineInput->clear();
    }

    // Passive edit check
    if (!currentChatPath.isEmpty() && currentLastNodeId != 0 && !isCreatingNewChat) {
        QTextDocument* doc = chatTextArea->document();
        QString currentRole = "";
        QString currentContent = "";
        int msgIndex = 0;
        bool forkCreated = false;

        QString expectedUserBlock = QString("[%1]").arg(currentDb->getSetting("book", 0, "userName", "User"));
        QString expectedAssistantBlock =
            QString("[%1]").arg(currentDb->getSetting("book", 0, "assistantName", "Assistant"));

        for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
            QString blockText = block.text();
            if (blockText.trimmed() == expectedUserBlock || blockText.trimmed() == expectedAssistantBlock) {
                if (!currentRole.isEmpty() && msgIndex < currentChatPath.size()) {
                    if (currentRole == currentChatPath[msgIndex].role &&
                        currentContent.trimmed() != currentChatPath[msgIndex].content.trimmed()) {
                        int pId = (msgIndex > 0) ? currentChatPath[msgIndex - 1].id : 0;
                        int newFId = currentDb->addMessage(pId, currentContent.trimmed(), currentRole);
                        currentLastNodeId = newFId;
                        forkCreated = true;
                        break;
                    }
                    msgIndex++;
                }
                currentRole = blockText.trimmed() == expectedUserBlock ? "user" : "assistant";
                currentContent = "";
                continue;
            }
            if (!currentRole.isEmpty()) {
                currentContent += blockText + "\n";
            }
        }

        if (!forkCreated && !currentRole.isEmpty() && msgIndex < currentChatPath.size()) {
            if (currentRole == currentChatPath[msgIndex].role &&
                currentContent.trimmed() != currentChatPath[msgIndex].content.trimmed()) {
                int pId = (msgIndex > 0) ? currentChatPath[msgIndex - 1].id : 0;
                int newFId = currentDb->addMessage(pId, currentContent.trimmed(), currentRole);
                currentLastNodeId = newFId;
                forkCreated = true;
            }
        }

        if (forkCreated) {
            QList<MessageNode> allMsgs = currentDb->getMessages();
            currentChatPath.clear();
            getPathToRoot(currentLastNodeId, allMsgs, currentChatPath);
            statusBar->showMessage(tr("Edits resulted in a new fork."), 3000);
        }
    }

    int parentId = isCreatingNewChat ? 0 : currentLastNodeId;
    int folderId = isCreatingNewChat ? currentChatFolderId : 0;
    isCreatingNewChat = false;

    // 1. Add User message
    int userMsgId = currentDb->addMessage(parentId, text, "user", folderId);

    // 2. Add Assistant placeholder
    int aiId = currentDb->addMessage(userMsgId, "...", "assistant");
    currentLastNodeId = aiId;

    // 3. Enqueue
    QueueManager::instance().enqueuePrompt(aiId, m_selectedModel, text);

    // 4. Refresh tree if needed (especially for new chats)
    QStandardItem* chatsFolder = nullptr;
    if (openBooksModel && openBooksModel->rowCount() > 0) {
        QStandardItem* bookItem = openBooksModel->item(0);  // Assume first for now or find correct one
        for (int j = 0; j < bookItem->rowCount(); ++j) {
            if (bookItem->child(j)->data(Qt::UserRole + 1).toString() == "chats_folder") {
                chatsFolder = bookItem->child(j);
                break;
            }
        }
    }
    if (chatsFolder) {
        chatsFolder->removeRows(0, chatsFolder->rowCount());
        populateChatFolders(chatsFolder, 0, currentDb->getMessages(), currentDb.get());
        openBooksTree->expandAll();
    }

    // 5. Update view
    updateLinearChatView(currentLastNodeId, currentDb->getMessages());
    statusBar->showMessage(tr("Request queued."), 3000);
}

void MainWindow::onOllamaChunk(const QString& chunk) {}
void MainWindow::onOllamaComplete(const QString& fullResponse) {}
void MainWindow::onOllamaError(const QString& error) {}

void MainWindow::onQueueChunk(std::shared_ptr<BookDatabase> db, int messageId, const QString& chunk) {
    if (currentDb == db && currentLastNodeId == messageId) {
        chatTextArea->blockSignals(true);
        QTextCursor cursor = chatTextArea->textCursor();
        cursor.movePosition(QTextCursor::End);
        chatTextArea->setTextCursor(cursor);
        chatTextArea->insertPlainText(chunk);
        chatTextArea->blockSignals(false);

        if (!currentChatPath.isEmpty() && currentChatPath.last().id == messageId) {
            currentChatPath.last().content += chunk;
        }
    }
}

void MainWindow::onProcessingStarted(std::shared_ptr<BookDatabase> db, int messageId) {
    if (currentDb == db && currentLastNodeId == messageId) {
        statusBar->showMessage(tr("LLM is responding..."));
    }
    updateQueueStatus();
}

void MainWindow::onProcessingFinished(std::shared_ptr<BookDatabase> db, int messageId, bool success) {
    if (currentDb == db && currentLastNodeId == messageId) {
        statusBar->showMessage(success ? tr("Response complete.") : tr("Error in response."), 3000);
        updateLinearChatView(currentLastNodeId, db->getMessages());
    }
    updateQueueStatus();
}

void MainWindow::onPullProgressUpdated(const QString& modelName, int percent, const QString& status) {
    statusLabel->setText(QString("Downloading %1: %2% (%3)").arg(modelName).arg(percent).arg(status));
}

void MainWindow::onPullFinished(const QString& modelName) {
    statusLabel->setText(QString("Download complete: %1").arg(modelName));
    QTimer::singleShot(3000, this, [this]() {
        if (statusLabel->text().startsWith("Download complete:")) {
            statusLabel->setText(tr("Ready"));
        }
    });
}

void MainWindow::onItemChanged(QStandardItem* item) {
    if (!currentDb) return;
    int id = item->data(Qt::UserRole).toInt();
    QString newText = item->text();
    int bracketIndex = newText.indexOf("] ");
    if (bracketIndex != -1) {
        newText = newText.mid(bracketIndex + 2);
    }
    currentDb->updateMessage(id, newText);
}

void MainWindow::onChatNodeSelected(const QModelIndex& current, const QModelIndex& /*previous*/) {
    if (!current.isValid()) return;
    QStandardItem* item = chatModel->itemFromIndex(current);
    if (item) {
        int previewNodeId = item->data(Qt::UserRole).toInt();
        if (currentDb) {
            updateLinearChatView(previewNodeId, currentDb->getMessages());
            updateInputBehavior();  // Keep it updated when selecting nodes/chats
        }
    }
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
    if (obj->property("is_breadcrumb_edit").toBool()) {
        if (event->type() == QEvent::MouseButtonDblClick) {
            QLineEdit* edit = qobject_cast<QLineEdit*>(obj);
            if (edit && edit->isReadOnly()) {
                edit->setReadOnly(false);
                edit->setFocus();
                edit->selectAll();
                return true;
            }
        }
    }

    if (event->type() == QEvent::DragEnter) {
        QDragEnterEvent* dragEvent = static_cast<QDragEnterEvent*>(event);
        if (dragEvent->mimeData()->hasUrls()) {
            dragEvent->acceptProposedAction();
            return true;
        }
        if (obj == openBooksTree && dragEvent->source() == bookList) {
            dragEvent->acceptProposedAction();
            return true;
        }
        if (obj == bookList && dragEvent->source() == openBooksTree) {
            dragEvent->acceptProposedAction();
            return true;
        }
        if (dragEvent->source() == openBooksTree || dragEvent->source() == vfsExplorer) {
            dragEvent->acceptProposedAction();
            return true;
        }
    } else if (event->type() == QEvent::DragMove) {
        QDragMoveEvent* dragEvent = static_cast<QDragMoveEvent*>(event);
        if (obj == openBooksTree) {
            QModelIndex index = openBooksTree->indexAt(dragEvent->position().toPoint());
            if (index.isValid()) {
                QStandardItem* targetItem = openBooksModel->itemFromIndex(index);
                QString targetType = targetItem->data(Qt::UserRole + 1).toString();
                if (targetType.endsWith("_folder")) {
                    dragEvent->acceptProposedAction();
                    return true;
                }
            }
        } else if (obj == vfsExplorer) {
            QModelIndex index = vfsExplorer->indexAt(dragEvent->position().toPoint());
            if (index.isValid()) {
                QStandardItem* targetItem = vfsModel->itemFromIndex(index);
                QString targetType = targetItem->data(Qt::UserRole + 1).toString();
                if (targetType.endsWith("_folder")) {
                    dragEvent->acceptProposedAction();
                    return true;
                }
            } else {
                dragEvent->acceptProposedAction();
                return true;
            }
        }
    } else if (event->type() == QEvent::Drop) {
        QDropEvent* dropEvent = static_cast<QDropEvent*>(event);
        if (dropEvent->mimeData()->hasUrls()) {
            // external files
            QList<QUrl> urls = dropEvent->mimeData()->urls();
            if (!urls.isEmpty() && urls.first().isLocalFile()) {
                QString filePath = urls.first().toLocalFile();
                QFileInfo fileInfo(filePath);
                if (fileInfo.suffix() == "db") {
                    handleBookDrop(fileInfo.fileName());
                    dropEvent->acceptProposedAction();
                    return true;
                }
            }
        } else if ((obj == openBooksTree || obj == vfsExplorer) &&
                   (dropEvent->source() == openBooksTree || dropEvent->source() == vfsExplorer)) {
            QAbstractItemView* targetView = qobject_cast<QAbstractItemView*>(obj);
            QAbstractItemView* sourceView = qobject_cast<QAbstractItemView*>(dropEvent->source());

            QModelIndex targetIndex = targetView->indexAt(dropEvent->position().toPoint());
            QStandardItem* targetItem = nullptr;

            if (targetIndex.isValid()) {
                targetItem = (obj == openBooksTree) ? openBooksModel->itemFromIndex(targetIndex)
                                                    : vfsModel->itemFromIndex(targetIndex);
            } else if (obj == vfsExplorer) {
                // Background drop in VFS explorer - move to current viewing folder
                QModelIndex treeIndex = openBooksTree->currentIndex();
                if (treeIndex.isValid()) targetItem = openBooksModel->itemFromIndex(treeIndex);
            }

            if (targetItem) {
                QModelIndex sourceIndex = sourceView->currentIndex();
                if (sourceIndex.isValid()) {
                    QStandardItem* draggedItem = nullptr;
                    if (dropEvent->source() == openBooksTree) {
                        draggedItem = openBooksModel->itemFromIndex(sourceIndex);
                    } else if (dropEvent->source() == vfsExplorer) {
                        QStandardItem* vfsItem = vfsModel->itemFromIndex(sourceIndex);
                        if (vfsItem && vfsItem->text() != "..") {
                            int id = vfsItem->data(Qt::UserRole).toInt();
                            QString type = vfsItem->data(Qt::UserRole + 1).toString();
                            QModelIndex treeIndex = openBooksTree->currentIndex();
                            if (treeIndex.isValid()) {
                                QStandardItem* currentBookOrFolder = openBooksModel->itemFromIndex(treeIndex);
                                QStandardItem* book = currentBookOrFolder;
                                while (book && book->parent()) book = book->parent();
                                draggedItem = findItemRecursive(book, id, type);
                            }
                        }
                    }

                    if (draggedItem &&
                        moveItemToFolder(draggedItem, targetItem, dropEvent->dropAction() == Qt::CopyAction)) {
                        dropEvent->acceptProposedAction();
                        return true;
                    }
                }
            }
        } else if (obj == openBooksTree && dropEvent->source() == bookList) {
            QListWidgetItem* item = bookList->currentItem();
            if (item) {
                QString fileName = item->text();
                handleBookDrop(fileName);
                dropEvent->acceptProposedAction();
                return true;
            }
        } else if (obj == bookList && dropEvent->source() == openBooksTree) {
            QModelIndex index = openBooksTree->currentIndex();
            if (index.isValid()) {
                QStandardItem* item = openBooksModel->itemFromIndex(index);
                if (item && !item->parent()) {  // Ensure it's a root item (book)
                    QString fileName = item->text();
                    closeBook(fileName);
                    dropEvent->acceptProposedAction();
                    return true;
                }
            }
        }
    }
    return KXmlGuiWindow::eventFilter(obj, event);
}

void MainWindow::handleBookDrop(const QString& fileName) {
    // Find the item in bookList and click it programmatically or just replicate the open logic
    for (int i = 0; i < bookList->count(); ++i) {
        if (bookList->item(i)->text() == fileName) {
            onBookSelected(bookList->model()->index(i, 0));
            return;
        }
    }
}

void MainWindow::closeBook(const QString& fileName) {
    // Remove from tree
    for (int i = 0; i < openBooksModel->rowCount(); ++i) {
        if (openBooksModel->item(i)->text() == fileName) {
            openBooksModel->removeRow(i);
            break;
        }
    }
    // Add back to list
    bookList->addItem(fileName);

    // Clear current db if closed
    if (m_openDatabases.contains(fileName)) {
        auto db = m_openDatabases[fileName];
        QueueManager::instance().removeDatabase(db);
        db->close();
        m_openDatabases.remove(fileName);

        if (currentDb == db) {
            currentDb = nullptr;
            chatModel->clear();
            statusLabel->setText(tr("Ready"));
            mainContentStack->setCurrentWidget(emptyView);
        }
    }
}

void MainWindow::showBookContextMenu(const QPoint& pos) {
    QListWidgetItem* item = bookList->itemAt(pos);
    if (!item) return;

    QMenu menu(this);
    QAction* openAction = menu.addAction("Open");
    QAction* openLocationAction = menu.addAction(QIcon::fromTheme("folder-open"), "Open book location");
    QAction* deleteAction = menu.addAction("Delete");

    // Favorite support
    QSettings settings;
    QStringList favorites = settings.value("favorites").toStringList();
    QString fileName = item->text();
    bool isFavorite = favorites.contains(fileName);

    QAction* favoriteAction = menu.addAction(isFavorite ? "Unfavorite" : "Favorite");
    if (isFavorite) {
        favoriteAction->setIcon(QIcon::fromTheme("emblem-favorite"));
    } else {
        favoriteAction->setIcon(QIcon::fromTheme("emblem-favorite-symbolic"));
    }

    QAction* selectedAction = menu.exec(bookList->viewport()->mapToGlobal(pos));
    if (selectedAction == openAction) {
        onBookSelected(bookList->model()->index(bookList->row(item), 0));
    } else if (selectedAction == openLocationAction) {
        onOpenBookLocation();
    } else if (selectedAction == deleteAction) {
        QString fileName = item->text();
        QString filePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/" + fileName;
        QFile::remove(filePath);
        delete bookList->takeItem(bookList->row(item));
    } else if (selectedAction == favoriteAction) {
        if (isFavorite) {
            favorites.removeAll(fileName);
            item->setIcon(QIcon());  // remove icon
        } else {
            favorites.append(fileName);
            item->setIcon(QIcon::fromTheme("emblem-favorite"));
        }
        settings.setValue("favorites", favorites);
        // Re-sort or reload list to show favorites at top
        loadBooks();
    }
}

void MainWindow::onOpenBooksSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected) {
    if (selected.indexes().isEmpty()) {
        mainContentStack->setCurrentWidget(emptyView);
        return;
    }

    QModelIndex index = selected.indexes().first();
    QStandardItem* item = openBooksModel->itemFromIndex(index);
    if (!item) {
        mainContentStack->setCurrentWidget(emptyView);
        return;
    }

    currentAutoDraftId = 0;  // Reset auto draft when selecting a new item

    QString type = item->data(Qt::UserRole + 1).toString();
    if (type == "book" || type == "chats_folder" || type == "docs_folder" || type == "notes_folder" ||
        type == "templates_folder" || type == "drafts_folder") {
        vfsModel->clear();

        // Add virtual ".." folder for non-book items (books are at root)
        if (type != "book") {
            QStandardItem* upItem = new QStandardItem(QIcon::fromTheme("go-up"), "..");
            vfsModel->appendRow(upItem);

            // If it's the chats_folder, sync currentLastNodeId = 0
            if (type == "chats_folder") {
                currentLastNodeId = 0;
            }
        }

        for (int i = 0; i < item->rowCount(); ++i) {
            QStandardItem* childTreeItem = item->child(i);
            QStandardItem* childItem = new QStandardItem(childTreeItem->icon(), childTreeItem->text());
            childItem->setData(childTreeItem->data(Qt::UserRole), Qt::UserRole);
            childItem->setData(childTreeItem->data(Qt::UserRole + 1), Qt::UserRole + 1);
            vfsModel->appendRow(childItem);
        }

        mainContentStack->setCurrentWidget(vfsExplorer);
    } else {
        int nodeId = item->data(Qt::UserRole).toInt();
        if (type == "document" || type == "template" || type == "draft") {
            currentDocumentId = nodeId;
            isCreatingNewDoc = (nodeId == 0);
            if (isCreatingNewDoc) {
                documentEditorView->clear();
                mainContentStack->setCurrentWidget(docContainer);
            } else if (currentDb) {
                QList<DocumentNode> docs =
                    (type == "template") ? currentDb->getTemplates()
                                         : ((type == "draft") ? currentDb->getDrafts() : currentDb->getDocuments());
                for (const auto& doc : docs) {
                    if (doc.id == currentDocumentId) {
                        documentEditorView->setPlainText(doc.content);
                        mainContentStack->setCurrentWidget(docContainer);
                        break;
                    }
                }
            }
        } else if (type == "note") {
            currentNoteId = nodeId;
            isCreatingNewNote = (nodeId == 0);
            if (isCreatingNewNote) {
                noteEditorView->clear();
                mainContentStack->setCurrentWidget(noteContainer);
            } else {
                // For notes, we just use the selected item's text if possible,
                // but let's fetch content from DB to be sure
                if (currentDb) {
                    QList<NoteNode> notes = currentDb->getNotes();
                    for (const auto& note : notes) {
                        if (note.id == currentNoteId) {
                            noteEditorView->setPlainText(note.content);
                            mainContentStack->setCurrentWidget(noteContainer);
                            break;
                        }
                    }
                }
            }
        } else {
            // It's a chat leaf or fork point
            currentLastNodeId = nodeId;
            isCreatingNewChat = (nodeId == 0);
            if (isCreatingNewChat && item->parent()) {
                currentChatFolderId = item->parent()->data(Qt::UserRole).toInt();
            }
            if (currentDb) {
                updateLinearChatView(currentLastNodeId, currentDb->getMessages());
                mainContentStack->setCurrentWidget(chatWindowView);
            }
        }
    }

    updateBreadcrumbs();
}

void MainWindow::loadDocumentsAndNotes() {
    if (!openBooksModel) return;

    for (int i = 0; i < openBooksModel->rowCount(); ++i) {
        QStandardItem* bookItem = openBooksModel->item(i);
        auto db = m_openDatabases.value(bookItem->text());
        if (!db || !db->isOpen()) continue;

        QStandardItem* docsFolder = nullptr;
        QStandardItem* templatesFolder = nullptr;
        QStandardItem* draftsFolder = nullptr;
        QStandardItem* notesFolder = nullptr;
        QStandardItem* chatsFolder = nullptr;

        for (int j = 0; j < bookItem->rowCount(); ++j) {
            QString type = bookItem->child(j)->data(Qt::UserRole + 1).toString();
            if (type == "docs_folder")
                docsFolder = bookItem->child(j);
            else if (type == "templates_folder")
                templatesFolder = bookItem->child(j);
            else if (type == "drafts_folder")
                draftsFolder = bookItem->child(j);
            else if (type == "notes_folder")
                notesFolder = bookItem->child(j);
            else if (type == "chats_folder")
                chatsFolder = bookItem->child(j);
        }

        if (docsFolder) {
            docsFolder->removeRows(0, docsFolder->rowCount());
            populateDocumentFolders(docsFolder, 0, "documents", db.get());
        }
        if (templatesFolder) {
            templatesFolder->removeRows(0, templatesFolder->rowCount());
            populateDocumentFolders(templatesFolder, 0, "templates", db.get());
        }
        if (draftsFolder) {
            draftsFolder->removeRows(0, draftsFolder->rowCount());
            populateDocumentFolders(draftsFolder, 0, "drafts", db.get());
        }
        if (notesFolder) {
            notesFolder->removeRows(0, notesFolder->rowCount());
            populateDocumentFolders(notesFolder, 0, "notes", db.get());
        }
        if (chatsFolder) {
            chatsFolder->removeRows(0, chatsFolder->rowCount());
            populateChatFolders(chatsFolder, 0, db->getMessages(), db.get());
        }
    }

    QModelIndex currentIndex = openBooksTree->currentIndex();
    if (currentIndex.isValid()) {
        QStandardItem* currentItem = openBooksModel->itemFromIndex(currentIndex);
        if (currentItem) {
            QString type = currentItem->data(Qt::UserRole + 1).toString();
            if (type == "docs_folder" || type == "templates_folder" || type == "drafts_folder" ||
                type == "notes_folder" || type == "chats_folder") {
                emit openBooksTree->selectionModel()->selectionChanged(QItemSelection(currentIndex, currentIndex),
                                                                       QItemSelection());
            }
        }
    }
}

// Replaced showDocumentsContextMenu and showNotesContextMenu with showVfsContextMenu

void MainWindow::showOpenBookContextMenu(const QPoint& pos) {
    QModelIndex index = openBooksTree->indexAt(pos);
    if (!index.isValid()) return;

    QStandardItem* item = openBooksModel->itemFromIndex(index);
    if (!item) return;

    QString type = item->data(Qt::UserRole + 1).toString();

    // Only show close for root items (books)
    if (type == "book") {
        QMenu menu(this);
        QAction* settingsAction = menu.addAction(QIcon::fromTheme("configure"), "Settings");
        QAction* openLocAction = menu.addAction(QIcon::fromTheme("folder-open"), "Open book location");
        QAction* debugAction = menu.addAction(QIcon::fromTheme("tools-report-bug"), "Debug Info");
        menu.addSeparator();
        QAction* closeAction = menu.addAction("Close");

        QAction* selectedAction = menu.exec(openBooksTree->viewport()->mapToGlobal(pos));
        if (selectedAction == closeAction) {
            closeBook(item->text());
        } else if (selectedAction == openLocAction) {
            onOpenBookLocation();
        } else if (selectedAction == debugAction) {
            if (currentDb) {
                QString info = currentDb->getDatabaseDebugInfo();
                QMessageBox::information(this, "Database Debug Info", info);
            }
        } else if (selectedAction == settingsAction) {
            if (currentDb) {
                DatabaseSettingsDialog dlg(currentDb.get(), m_availableModels, this);
                if (dlg.exec() == QDialog::Accepted) {
                    QString dbModel = currentDb->getSetting("book", 0, "defaultModel", "");
                    if (!dbModel.isEmpty()) {
                        int idx = modelComboBox->findText(dbModel);
                        if (idx >= 0) {
                            modelComboBox->setCurrentIndex(idx);
                        }
                    } else if (!m_availableModels.isEmpty()) {
                        modelComboBox->setCurrentIndex(0);  // Revert to global fallback
                    }
                    if (currentLastNodeId != 0 && mainContentStack->currentWidget() == chatWindowView) {
                        updateLinearChatView(currentLastNodeId, currentDb->getMessages());
                    }
                }
            }
        }
    } else if (type == "chats_folder" || type == "docs_folder" || type == "notes_folder" ||
               type == "templates_folder" || type == "drafts_folder") {
        QMenu menu(this);
        QString actionName = "New Item";
        if (type == "chats_folder")
            actionName = "New Chat";
        else if (type == "docs_folder")
            actionName = "New Document";
        else if (type == "notes_folder")
            actionName = "New Note";
        else if (type == "templates_folder")
            actionName = "New Template";
        else if (type == "drafts_folder")
            actionName = "New Draft";

        QAction* newAction = menu.addAction(actionName);

        QAction* createFolderAction = nullptr;
        if (type == "docs_folder" || type == "templates_folder" || type == "drafts_folder" || type == "notes_folder" ||
            type == "chats_folder") {
            createFolderAction = menu.addAction(QIcon::fromTheme("folder-new"), "Create Folder");
        }

        QAction* importAction = nullptr;
        if (type == "chats_folder") {
            importAction = menu.addAction("Import Chat Session");
        }

        QAction* selectedAction = menu.exec(openBooksTree->viewport()->mapToGlobal(pos));
        if (selectedAction == newAction) {
            addPhantomItem(item, type);
        } else if (createFolderAction && selectedAction == createFolderAction) {
            bool ok;
            QString name = QInputDialog::getText(this, "Create Folder", "Enter folder name:", QLineEdit::Normal,
                                                 "New Folder", &ok);
            if (ok && !name.isEmpty() && currentDb) {
                int parentId = item->data(Qt::UserRole).toInt();
                QString fType = "documents";
                if (type == "templates_folder")
                    fType = "templates";
                else if (type == "drafts_folder")
                    fType = "drafts";
                else if (type == "notes_folder")
                    fType = "notes";
                else if (type == "chats_folder")
                    fType = "chats";

                currentDb->addFolder(parentId, name, fType);
                loadDocumentsAndNotes();
            }
        } else if (importAction && selectedAction == importAction) {
            importChatSession();
        }
    } else if (type == "chat_session") {
        QMenu menu(this);
        QAction* exportAction = menu.addAction("Export Chat Session");

        QAction* selectedAction = menu.exec(openBooksTree->viewport()->mapToGlobal(pos));
        if (selectedAction == exportAction) {
            exportChatSession();
        }
    }
}

void MainWindow::exportChatSession() {
    if (!currentDb) return;

    // For now we assume all messages in the db are part of the session, but we will add an ID.
    // However the current code just loads everything as one session.
    // Let's implement exporting all messages in the DB for now since that's what the current DB does.

    QString fileName =
        QFileDialog::getSaveFileName(this, tr("Export Chat Session"), QDir::homePath(), tr("JSON Files (*.json)"));
    if (fileName.isEmpty()) return;

    QList<MessageNode> allMessages = currentDb->getMessages();
    QJsonArray messagesArray;
    for (const auto& msg : allMessages) {
        QJsonObject msgObj;
        msgObj["id"] = msg.id;
        msgObj["parentId"] = msg.parentId;
        msgObj["role"] = msg.role;
        msgObj["content"] = msg.content;
        msgObj["timestamp"] = msg.timestamp.toString(Qt::ISODate);
        messagesArray.append(msgObj);
    }

    QJsonObject rootObj;
    rootObj["messages"] = messagesArray;
    rootObj["version"] = 1;

    QJsonDocument doc(rootObj);
    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson());
        file.close();
        statusBar->showMessage(tr("Chat session exported to %1").arg(fileName), 3000);
    } else {
        QMessageBox::warning(this, tr("Export Error"), tr("Could not save to file: %1").arg(file.errorString()));
    }
}

void MainWindow::importChatSession() {
    if (!currentDb) {
        QMessageBox::warning(this, tr("Import Error"), tr("Please open a book first."));
        return;
    }

    QString fileName =
        QFileDialog::getOpenFileName(this, tr("Import Chat Session"), QDir::homePath(), tr("JSON Files (*.json)"));
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Import Error"), tr("Could not open file: %1").arg(file.errorString()));
        return;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError) {
        QMessageBox::warning(this, tr("Import Error"), tr("Invalid JSON format: %1").arg(parseError.errorString()));
        return;
    }

    QJsonObject rootObj = doc.object();
    QJsonArray messagesArray = rootObj["messages"].toArray();

    // Map old IDs to new IDs
    QMap<int, int> idMapping;
    idMapping[0] = 0;  // Root is always 0

    for (const QJsonValue& val : messagesArray) {
        QJsonObject msgObj = val.toObject();
        int oldId = msgObj["id"].toInt();
        int oldParentId = msgObj["parentId"].toInt();
        QString role = msgObj["role"].toString();
        QString content = msgObj["content"].toString();

        int newParentId = idMapping.value(oldParentId, 0);  // Default to root if not found
        int newId = currentDb->addMessage(newParentId, content, role);
        if (newId != -1) {
            idMapping[oldId] = newId;
        }
    }

    loadSession(0);
    statusBar->showMessage(tr("Chat session imported successfully"), 3000);
}

QStandardItem* MainWindow::findItemInTree(int id, const QString& type) {
    if (!openBooksModel || openBooksModel->rowCount() == 0) return nullptr;
    return findItemRecursive(openBooksModel->invisibleRootItem(), id, type);
}

QStandardItem* MainWindow::findItemRecursive(QStandardItem* parent, int id, const QString& type) {
    if (parent->data(Qt::UserRole).toInt() == id && parent->data(Qt::UserRole + 1).toString() == type) {
        return parent;
    }
    for (int i = 0; i < parent->rowCount(); ++i) {
        QStandardItem* found = findItemRecursive(parent->child(i), id, type);
        if (found) return found;
    }
    return nullptr;
}

void MainWindow::updateQueueStatus() {
    int total = QueueManager::instance().totalPendingCount();
    queueStatusBtn->setText(QString("Q: %1").arg(total));
    if (QueueManager::instance().isProcessing()) {
        queueStatusBtn->setStyleSheet("background-color: #2196F3; color: white;");
    } else {
        queueStatusBtn->setStyleSheet("");
    }
}

void MainWindow::updateNotificationStatus() {
    int total = 0;
    notificationMenu->clear();
    for (auto db : QueueManager::instance().databases()) {
        if (!db || !db->isOpen()) continue;
        auto notifications = db->getNotifications();
        total += notifications.size();
        for (const auto& n : notifications) {
            QString bookName = QFileInfo(db->filepath()).fileName();
            QString typeStr = (n.type == "error") ? tr("Error") : tr("Finished");
            QAction* action = notificationMenu->addAction(
                QString("[%1] %2: Msg %3").arg(bookName, typeStr, QString::number(n.messageId)));
            connect(action, &QAction::triggered, this, [this, db, n]() {
                onQueueItemClicked(db, n.messageId);
                db->dismissNotification(n.id);
                updateNotificationStatus();
            });
        }
    }
    notificationBtn->setText(QString("🔔 %1").arg(total));
    if (total > 0) {
        notificationBtn->setStyleSheet("background-color: #f44336; color: white;");
    } else {
        notificationBtn->setStyleSheet("");
    }

    // Live update tree markers
    for (int i = 0; i < openBooksModel->rowCount(); ++i) {
        QStandardItem* bookItem = openBooksModel->item(i);
        QString filePath = bookItem->data(Qt::UserRole + 2).toString();
        QString fileName = QFileInfo(filePath).fileName();
        if (m_openDatabases.contains(fileName)) {
            auto notifications = m_openDatabases[fileName]->getNotifications();
            updateTreeMarkersRecursive(bookItem, notifications);

            // Also update VFS and Fork explorers if they are currently displaying items for this DB
            if (currentDb && currentDb->filepath() == filePath) {
                updateVfsMarkers(notifications);
            }
        }
    }

    openBooksModel->layoutChanged();
}

void MainWindow::showNotificationMenu() { notificationBtn->showMenu(); }

void MainWindow::showQueueWindow() {
    QueueWindow* qw = new QueueWindow(this);
    qw->setAttribute(Qt::WA_DeleteOnClose);
    qw->show();
}

void MainWindow::onQueueItemClicked(std::shared_ptr<BookDatabase> db, int messageId) {
    if (!db) return;

    // Find book index
    QString bookFileName = QFileInfo(db->filepath()).fileName();
    for (int i = 0; i < openBooksModel->rowCount(); ++i) {
        if (openBooksModel->item(i)->text() == bookFileName) {
            openBooksTree->setCurrentIndex(openBooksModel->item(i)->index());
            currentDb = db;
            QueueManager::instance().setActiveDatabase(db);
            break;
        }
    }

    currentLastNodeId = messageId;
    updateLinearChatView(currentLastNodeId, db->getMessages());
    mainContentStack->setCurrentWidget(chatWindowView);
    db->dismissNotificationByMessageId(messageId);
    updateNotificationStatus();
}

void MainWindow::updateVfsMarkers(const QList<Notification>& notifications) {
    if (vfsModel) {
        for (int i = 0; i < vfsModel->rowCount(); ++i) {
            QStandardItem* item = vfsModel->item(i);
            int id = item->data(Qt::UserRole).toInt();
            int nType = 0;
            for (const auto& n : notifications) {
                if (n.messageId == id && !n.isDismissed) {
                    nType = (n.type == "error") ? 2 : 1;
                    break;
                }
            }
            item->setData(nType, Qt::UserRole + 10);
        }
    }
    if (forkExplorerModel) {
        for (int i = 0; i < forkExplorerModel->rowCount(); ++i) {
            QStandardItem* item = forkExplorerModel->item(i);
            int id = item->data(Qt::UserRole).toInt();
            int nType = 0;
            for (const auto& n : notifications) {
                if (n.messageId == id && !n.isDismissed) {
                    nType = (n.type == "error") ? 2 : 1;
                    break;
                }
            }
            item->setData(nType, Qt::UserRole + 10);
        }
    }
}

void MainWindow::updateTreeMarkersRecursive(QStandardItem* parent, const QList<Notification>& notifications) {
    if (!parent) return;
    for (int i = 0; i < parent->rowCount(); ++i) {
        QStandardItem* child = parent->child(i);
        if (!child) continue;
        int messageId = child->data(Qt::UserRole).toInt();
        int notifyType = 0;
        for (const auto& n : notifications) {
            if (n.messageId == messageId && !n.isDismissed) {
                notifyType = (n.type == "error") ? 2 : 1;
                break;
            }
        }
        child->setData(notifyType, Qt::UserRole + 10);
        updateTreeMarkersRecursive(child, notifications);
    }
}

bool MainWindow::moveItemToFolder(QStandardItem* draggedItem, QStandardItem* targetItem, bool isCopy) {
    if (!draggedItem || !targetItem) return false;

    // Find root book items
    QStandardItem* sourceBook = draggedItem;
    while (sourceBook && sourceBook->parent()) sourceBook = sourceBook->parent();
    QStandardItem* targetBook = targetItem;
    while (targetBook && targetBook->parent()) targetBook = targetBook->parent();

    if (!sourceBook || !targetBook || sourceBook != targetBook) return false;

    // Find database
    auto db = m_openDatabases.value(targetBook->text());
    if (!db) return false;

    QString itemType = draggedItem->data(Qt::UserRole + 1).toString();
    int itemId = draggedItem->data(Qt::UserRole).toInt();
    QString targetType = targetItem->data(Qt::UserRole + 1).toString();
    int targetFolderId = targetItem->data(Qt::UserRole).toInt();

    if (draggedItem == targetItem) return false;

    // Cycle detection for folder moves
    if (itemType.endsWith("_folder")) {
        QStandardItem* temp = targetItem;
        while (temp) {
            if (temp == draggedItem) return false;
            temp = temp->parent();
        }
    }

    // Handle dropping on an item (non-folder)
    if (!targetType.endsWith("_folder")) {
        if (targetItem->parent()) {
            targetItem = targetItem->parent();
            targetType = targetItem->data(Qt::UserRole + 1).toString();
            targetFolderId = targetItem->data(Qt::UserRole).toInt();
        } else {
            return false;
        }
    }

    if (isCopy) {
        bool copied = false;
        if (itemType == "document" && targetType == "docs_folder") {
            for (const auto& d : db->getDocuments(-1))
                if (d.id == itemId) {
                    db->addDocument(targetFolderId, "Copy of " + d.title, d.content);
                    copied = true;
                }
        } else if (itemType == "template" && targetType == "templates_folder") {
            for (const auto& d : db->getTemplates(-1))
                if (d.id == itemId) {
                    db->addTemplate(targetFolderId, "Copy of " + d.title, d.content);
                    copied = true;
                }
        } else if (itemType == "draft" && targetType == "drafts_folder") {
            for (const auto& d : db->getDrafts(-1))
                if (d.id == itemId) {
                    db->addDraft(targetFolderId, "Copy of " + d.title, d.content);
                    copied = true;
                }
        } else if (itemType == "note" && targetType == "notes_folder") {
            for (const auto& d : db->getNotes(-1))
                if (d.id == itemId) {
                    db->addNote(targetFolderId, "Copy of " + d.title, d.content);
                    copied = true;
                }
        } else if (itemType == "chat_session" && targetType == "chats_folder") {
            // complex copy omitted for now
        }
        if (copied) {
            loadDocumentsAndNotes();
            return true;
        }
        return false;
    }
    bool compatible = false;
    QString table;
    if (itemType == "chat_session" && targetType == "chats_folder") {
        table = "messages";
        compatible = true;
    } else if (itemType == "document" && targetType == "docs_folder") {
        table = "documents";
        compatible = true;
    } else if (itemType == "template" && targetType == "templates_folder") {
        table = "templates";
        compatible = true;
    } else if (itemType == "draft" && targetType == "drafts_folder") {
        table = "drafts";
        compatible = true;
    } else if (itemType == "note" && targetType == "notes_folder") {
        table = "notes";
        compatible = true;
    } else if (itemType.endsWith("_folder") && targetType == itemType) {
        if (db->moveFolder(itemId, targetFolderId)) {
            loadDocumentsAndNotes();
            return true;
        }
    }

    if (compatible && db->moveItem(table, itemId, targetFolderId)) {
        loadDocumentsAndNotes();  // This should be updated for the specific book if needed, but for now it's okay
        return true;
    }
    return false;
}
