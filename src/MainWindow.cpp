#include "MainWindow.h"

#include <QTextBlock>
#include <KActionCollection>
#include <KActionMenu>
#include <KStandardAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDate>
#include <QDebug>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QLocale>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QScreen>
#include <QSettings>
#include <QSpinBox>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>
#include <QtGui/QAction>
#include <limits>
#include <QStandardPaths>
#include <QDir>
#include <QInputDialog>
#include "WalletManager.h"
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>

MainWindow::MainWindow(QWidget *parent)
    : KXmlGuiWindow(parent),
      currentLastNodeId(0) {

    setupUi();
    setupWindow();
}

MainWindow::~MainWindow() {}

void MainWindow::closeEvent(QCloseEvent *event) {
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
    openBooksTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    openBooksTree->setDropIndicatorShown(true);
    openBooksTree->installEventFilter(this);
    openBooksTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(openBooksTree, &QWidget::customContextMenuRequested, this, &MainWindow::showOpenBookContextMenu);

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
    leftSplitter->setStretchFactor(0, 3); // 60% approx
    leftSplitter->setStretchFactor(1, 2); // 40% approx

    splitter->addWidget(leftSplitter);

    QWidget* rightWidget = new QWidget(this);
    QVBoxLayout* rightLayout = new QVBoxLayout(rightWidget);

    mainContentStack = new QStackedWidget(this);

    emptyView = new QWidget(this);
    mainContentStack->addWidget(emptyView);

    dbDirectView = new QListView(this);
    dbDirectModel = new QStandardItemModel(this);
    dbDirectView->setModel(dbDirectModel);
    dbDirectView->setViewMode(QListView::IconMode);
    dbDirectView->setEditTriggers(QAbstractItemView::NoEditTriggers);

    QStandardItem* chatsFolderItem = new QStandardItem(QIcon::fromTheme("folder-open"), "Chats");
    QStandardItem* docsFolderItem = new QStandardItem(QIcon::fromTheme("folder-open"), "Documents");
    QStandardItem* notesFolderItem = new QStandardItem(QIcon::fromTheme("folder-open"), "Notes");
    dbDirectModel->appendRow(chatsFolderItem);
    dbDirectModel->appendRow(docsFolderItem);
    dbDirectModel->appendRow(notesFolderItem);

    mainContentStack->addWidget(dbDirectView);

    connect(dbDirectView, &QListView::doubleClicked, this, [this](const QModelIndex& index) {
        if (!index.isValid()) return;
        QString text = dbDirectModel->itemFromIndex(index)->text();

        if (openBooksModel->rowCount() > 0) {
            QStandardItem* rootItem = openBooksModel->item(0); // Assuming one open book for now
            for (int i = 0; i < rootItem->rowCount(); ++i) {
                QStandardItem* child = rootItem->child(i);
                if (child->text() == text) {
                    openBooksTree->setCurrentIndex(child->index());
                    break;
                }
            }
        }
    });

    chatsFolderView = new QTreeView(this);
    chatsFolderView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(chatsFolderView, &QWidget::customContextMenuRequested, this, &MainWindow::showChatTreeContextMenu);
    chatModel = new QStandardItemModel(this);
    chatsFolderView->setModel(chatModel);
    chatsFolderView->setHeaderHidden(true);
    chatsFolderView->setEditTriggers(QAbstractItemView::DoubleClicked);
    mainContentStack->addWidget(chatsFolderView);

    connect(chatsFolderView->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this](const QItemSelection& selected, const QItemSelection& deselected) {
        if (selected.indexes().isEmpty()) return;
        QModelIndex index = selected.indexes().first();
        QStandardItem* item = chatModel->itemFromIndex(index);
        if (!item) return;

        currentLastNodeId = item->data(Qt::UserRole).toInt();
        if (currentDb) {
            updateLinearChatView(currentLastNodeId, currentDb->getMessages());
            mainContentStack->setCurrentWidget(chatWindowView);
        }
    });

    chatTree = chatsFolderView; // Keep reference to avoid breaking old code for now

    documentsFolderView = new QTreeView(this);
    documentsModel = new QStandardItemModel(this);
    documentsFolderView->setModel(documentsModel);
    documentsFolderView->setHeaderHidden(true);
    documentsFolderView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(documentsFolderView, &QWidget::customContextMenuRequested, this, &MainWindow::showDocumentsContextMenu);
    mainContentStack->addWidget(documentsFolderView);

    notesFolderView = new QTreeView(this);
    notesModel = new QStandardItemModel(this);
    notesFolderView->setModel(notesModel);
    notesFolderView->setHeaderHidden(true);
    notesFolderView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(notesFolderView, &QWidget::customContextMenuRequested, this, &MainWindow::showNotesContextMenu);
    mainContentStack->addWidget(notesFolderView);

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
    saveNoteBtn = new QPushButton("Save Note", this);
    QPushButton* backToNotesBtn = new QPushButton("Back to Notes", this);
    QHBoxLayout* noteBtnLayout = new QHBoxLayout();
    noteBtnLayout->addWidget(backToNotesBtn);
    noteBtnLayout->addWidget(saveNoteBtn);
    noteLayout->addWidget(noteEditorView);
    noteLayout->addLayout(noteBtnLayout);
    mainContentStack->addWidget(noteContainer);

    connect(backToDocsBtn, &QPushButton::clicked, this, [this]() {
        mainContentStack->setCurrentWidget(documentsFolderView);
    });

    connect(backToNotesBtn, &QPushButton::clicked, this, [this]() {
        mainContentStack->setCurrentWidget(notesFolderView);
    });

    connect(documentsFolderView->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this](const QItemSelection& selected, const QItemSelection& deselected) {
        if (selected.indexes().isEmpty()) return;
        QModelIndex index = selected.indexes().first();
        QStandardItem* item = documentsModel->itemFromIndex(index);
        if (!item) return;

        currentDocumentId = item->data(Qt::UserRole).toInt();
        if (currentDb) {
            QList<DocumentNode> docs = currentDb->getDocuments();
            for (const auto& doc : docs) {
                if (doc.id == currentDocumentId) {
                    documentEditorView->setPlainText(doc.content);
                    mainContentStack->setCurrentWidget(docContainer);
                    break;
                }
            }
        }
    });

    connect(notesFolderView->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this](const QItemSelection& selected, const QItemSelection& deselected) {
        if (selected.indexes().isEmpty()) return;
        QModelIndex index = selected.indexes().first();
        QStandardItem* item = notesModel->itemFromIndex(index);
        if (!item) return;

        currentNoteId = item->data(Qt::UserRole).toInt();
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
    });

    connect(saveDocBtn, &QPushButton::clicked, this, [this]() {
        if (!currentDb || currentDocumentId == 0) return;
        QList<DocumentNode> docs = currentDb->getDocuments();
        for (const auto& doc : docs) {
            if (doc.id == currentDocumentId) {
                currentDb->updateDocument(currentDocumentId, doc.title, documentEditorView->toPlainText());
                statusBar->showMessage(tr("Document saved."), 3000);
                break;
            }
        }
    });

    connect(saveNoteBtn, &QPushButton::clicked, this, [this]() {
        if (!currentDb || currentNoteId == 0) return;
        QList<NoteNode> notes = currentDb->getNotes();
        for (const auto& note : notes) {
            if (note.id == currentNoteId) {
                currentDb->updateNote(currentNoteId, note.title, noteEditorView->toPlainText());
                statusBar->showMessage(tr("Note saved."), 3000);
                break;
            }
        }
    });

    chatWindowView = new QWidget(this);
    QVBoxLayout* chatLayout = new QVBoxLayout(chatWindowView);
    chatTextArea = new QTextEdit(this);
    // Left native standard context menu for basic copy/paste without block mapping
    chatLayout->addWidget(chatTextArea);

    QHBoxLayout* inputLayout = new QHBoxLayout();
    modelComboBox = new QComboBox(this);
    modelComboBox->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    inputLayout->addWidget(modelComboBox);

    toggleInputModeBtn = new QPushButton(QIcon::fromTheme("format-text-strikethrough"), "", this);
    toggleInputModeBtn->setToolTip(tr("Toggle Multi-line Input"));
    toggleInputModeBtn->setCheckable(true);
    inputLayout->addWidget(toggleInputModeBtn);

    inputModeStack = new QStackedWidget(this);
    inputField = new QLineEdit(this);
    inputModeStack->addWidget(inputField);

    multiLineInput = new QTextEdit(this);
    multiLineInput->setMaximumHeight(100);
    inputModeStack->addWidget(multiLineInput);

    inputLayout->addWidget(inputModeStack);

    saveEditsBtn = new QPushButton("Save Edits", this);
    saveEditsBtn->setToolTip(tr("Save modifications made to the chat history above."));
    inputLayout->addWidget(saveEditsBtn);

    sendButton = new QPushButton("Send", this);
    inputLayout->addWidget(sendButton);

    chatLayout->addLayout(inputLayout);

    connect(saveEditsBtn, &QPushButton::clicked, this, [this]() {
        if (!currentDb || currentChatPath.isEmpty()) return;

        // Instead of parsing flat text with regex, iterate blocks to reconstruct messages robustly
        QTextDocument* doc = chatTextArea->document();
        QString currentRole = "";
        QString currentContent = "";
        int msgIndex = 0;

        for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
            QString blockText = block.text();

            // Check if this block is a header
            if (blockText.trimmed() == "[User]") {
                // If we were already building a message, save it
                if (!currentRole.isEmpty() && msgIndex < currentChatPath.size()) {
                    if (currentRole == currentChatPath[msgIndex].role && currentContent.trimmed() != currentChatPath[msgIndex].content.trimmed()) {
                        currentDb->updateMessage(currentChatPath[msgIndex].id, currentContent.trimmed());
                        QStandardItem* foundItem = findItem(chatModel->invisibleRootItem(), currentChatPath[msgIndex].id);
                        if (foundItem) {
                            QString displayContent = currentContent.trimmed();
                            if (displayContent.contains('\n')) displayContent = displayContent.left(displayContent.indexOf('\n')) + " ...";
                            foundItem->setText(QString("[%1] %2").arg(currentChatPath[msgIndex].role, displayContent));
                        }
                    }
                    msgIndex++;
                }
                currentRole = "user";
                currentContent = "";
                continue;
            } else if (blockText.trimmed() == "[Assistant]") {
                if (!currentRole.isEmpty() && msgIndex < currentChatPath.size()) {
                    if (currentRole == currentChatPath[msgIndex].role && currentContent.trimmed() != currentChatPath[msgIndex].content.trimmed()) {
                        currentDb->updateMessage(currentChatPath[msgIndex].id, currentContent.trimmed());
                        QStandardItem* foundItem = findItem(chatModel->invisibleRootItem(), currentChatPath[msgIndex].id);
                        if (foundItem) {
                            QString displayContent = currentContent.trimmed();
                            if (displayContent.contains('\n')) displayContent = displayContent.left(displayContent.indexOf('\n')) + " ...";
                            foundItem->setText(QString("[%1] %2").arg(currentChatPath[msgIndex].role, displayContent));
                        }
                    }
                    msgIndex++;
                }
                currentRole = "assistant";
                currentContent = "";
                continue;
            }

            if (!currentRole.isEmpty()) {
                currentContent += blockText + "\n";
            }
        }

        // Save the very last message in the buffer
        if (!currentRole.isEmpty() && msgIndex < currentChatPath.size()) {
            if (currentRole == currentChatPath[msgIndex].role && currentContent.trimmed() != currentChatPath[msgIndex].content.trimmed()) {
                currentDb->updateMessage(currentChatPath[msgIndex].id, currentContent.trimmed());
                QStandardItem* foundItem = findItem(chatModel->invisibleRootItem(), currentChatPath[msgIndex].id);
                if (foundItem) {
                    QString displayContent = currentContent.trimmed();
                    if (displayContent.contains('\n')) displayContent = displayContent.left(displayContent.indexOf('\n')) + " ...";
                    foundItem->setText(QString("[%1] %2").arg(currentChatPath[msgIndex].role, displayContent));
                }
            }
        }

        updateLinearChatView(currentLastNodeId, currentDb->getMessages());
        statusBar->showMessage(tr("Chat edits saved successfully."), 3000);
    });

    mainContentStack->addWidget(chatWindowView);

    rightLayout->addWidget(mainContentStack);

    splitter->addWidget(rightWidget);

    connect(openBooksTree->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::onOpenBooksSelectionChanged);
    connect(toggleInputModeBtn, &QPushButton::toggled, this, [this](bool checked) {
        inputModeStack->setCurrentIndex(checked ? 1 : 0);
    });

    // Initial sizes
    int totalWidth = width();
    int leftWidth = qMax(totalWidth * 20 / 100, 100);
    int rightWidth = totalWidth - leftWidth;
    splitter->setSizes(QList<int>() << leftWidth << rightWidth);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    QToolBar* toolbar = addToolBar("Main");
    QAction* newBookAction = new QAction(QIcon::fromTheme("document-new"), tr("New Book"), this);
    actionCollection()->addAction(QStringLiteral("new_book"), newBookAction);
    toolbar->addAction(newBookAction);

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
    onConnectionStatusChanged(false); // Initially disconnected

    connect(endpointComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onActiveEndpointChanged);
    connect(&ollamaClient, &OllamaClient::connectionStatusChanged, this, &MainWindow::onConnectionStatusChanged);

    connect(newBookAction, &QAction::triggered, this, &MainWindow::onCreateBook);
    connect(bookList, &QListWidget::doubleClicked, this, &MainWindow::onBookSelected); // Open book from list via double click
    connect(sendButton, &QPushButton::clicked, this, &MainWindow::onSendMessage);
    connect(inputField, &QLineEdit::returnPressed, this, &MainWindow::onSendMessage);
    connect(chatModel, &QStandardItemModel::itemChanged, this, &MainWindow::onItemChanged);
    connect(chatTree->selectionModel(), &QItemSelectionModel::currentChanged, this, &MainWindow::onChatNodeSelected);

    connect(&ollamaClient, &OllamaClient::modelListUpdated, this, [this](const QStringList& models){
        modelComboBox->clear();

        QSettings settings;
        QStringList favorites = settings.value("favoriteModels").toStringList();

        for (const QString& model : models) {
            if (favorites.contains(model)) {
                modelComboBox->addItem("⭐ " + model, model);
            }
        }
        for (const QString& model : models) {
            if (!favorites.contains(model)) {
                modelComboBox->addItem(model, model);
            }
        }

        if (models.isEmpty()) {
            modelComboBox->addItem("llama2", "llama2"); // fallback
        }
    });

    connect(&ollamaClient, &OllamaClient::pullProgressUpdated, this, &MainWindow::onPullProgressUpdated);
    connect(&ollamaClient, &OllamaClient::pullFinished, this, &MainWindow::onPullFinished);

    QAction* modelExplorerAction = new QAction(QIcon::fromTheme("server-database"), tr("Model Explorer"), this);
    actionCollection()->addAction(QStringLiteral("model_explorer"), modelExplorerAction);
    connect(modelExplorerAction, &QAction::triggered, this, &MainWindow::showModelExplorer);

    QAction* settingsAction = new QAction(QIcon::fromTheme("preferences-system"), tr("Settings..."), this);
    actionCollection()->addAction(QStringLiteral("preferences"), settingsAction);
    connect(settingsAction, &QAction::triggered, this, &MainWindow::showSettingsDialog);

    QAction *quitAction = KStandardAction::quit(qApp, &QCoreApplication::quit, actionCollection());
    actionCollection()->addAction("quit", quitAction);

    QAction *aboutQtAction = new QAction(tr("About &Qt"), this);
    connect(aboutQtAction, &QAction::triggered, qApp, &QApplication::aboutQt);
    actionCollection()->addAction(QStringLiteral("about_qt"), aboutQtAction);

    setupGUI(Default, ":/kllamabooksui.rc");

    // Status Bar
    statusBar = new QStatusBar(this);
    setStatusBar(statusBar);

    statusLabel = new QLabel(tr("Ready"), this);
    statusBar->addWidget(statusLabel);

    modelLabel = new QLabel(tr("Model: Not Selected"), this);
    statusBar->addPermanentWidget(modelLabel);

    connect(modelComboBox, &QComboBox::currentTextChanged, this, [this](const QString &text) {
        if (text.isEmpty()) {
            modelLabel->setText(tr("Model: Not Selected"));
        } else {
            modelLabel->setText(tr("Model: %1").arg(text));
        }
    });

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
        endpointComboBox->setItemData(0, "", Qt::UserRole + 1); // Auth Key
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
    ollamaClient.fetchModels(); // Test connection and fetch

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
    });
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
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
    if(!dir.exists()) dir.mkpath(".");

    QSettings settings;
    QStringList favorites = settings.value("favorites").toStringList();

    QStringList filters;
    filters << "*.db";
    // Sort by last modified (accessed/changed)
    QFileInfoList fileList = dir.entryInfoList(filters, QDir::Files | QDir::NoDotAndDotDot, QDir::Time | QDir::Reversed);

    // Separate into favorites and others
    QList<QFileInfo> favList;
    QList<QFileInfo> otherList;

    for(const QFileInfo& file : fileList) {
        if (favorites.contains(file.fileName())) {
            favList.append(file);
        } else {
            otherList.append(file);
        }
    }

    // Sort within lists
    auto sortByTimeDesc = [](const QFileInfo& a, const QFileInfo& b) {
        return a.lastModified() > b.lastModified();
    };

    std::sort(favList.begin(), favList.end(), sortByTimeDesc);
    std::sort(otherList.begin(), otherList.end(), sortByTimeDesc);

    // Add favorites first
    for(const QFileInfo& file : favList) {
        QListWidgetItem* item = new QListWidgetItem(QIcon::fromTheme("emblem-favorite"), file.fileName());
        item->setToolTip(tr("Last modified: %1").arg(QLocale().toString(file.lastModified(), QLocale::ShortFormat)));
        bookList->addItem(item);
    }

    // Then add non-favorites
    for(const QFileInfo& file : otherList) {
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

void MainWindow::onCreateBook() {
    bool ok;
    QString bookName = QInputDialog::getText(this, "New Book", "Enter book name:", QLineEdit::Normal, "", &ok);
    if (ok && !bookName.isEmpty()) {
        QString password = QInputDialog::getText(this, "Password", "Enter password (optional):", QLineEdit::Password, "", &ok);
        if (ok) {
            bool savePassword = false;
            if (!password.isEmpty()) {
                QMessageBox::StandardButton reply = QMessageBox::question(this, "Save Password", "Do you want to save this password to KWallet?", QMessageBox::Yes | QMessageBox::No);
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
            newDb.open(password); // Creates the schema

            loadBooks();
        }
    }
}


void MainWindow::showChatTreeContextMenu(const QPoint& pos) {
    QMenu menu(this);
    QAction* newChatAction = menu.addAction(QIcon::fromTheme("chat-message-new"), "New Chat");
    menu.addSeparator();

    QModelIndex index = chatTree->indexAt(pos);
    QStandardItem* item = nullptr;
    QAction* copyAction = nullptr;
    QAction* pasteAction = nullptr;
    QAction* forkAction = nullptr;

    if (index.isValid()) {
        item = chatModel->itemFromIndex(index);
        if (item) {
            copyAction = menu.addAction(QIcon::fromTheme("edit-copy"), "Copy Message");
            pasteAction = menu.addAction(QIcon::fromTheme("edit-paste"), "Paste to Input");
            menu.addSeparator();
            forkAction = menu.addAction(QIcon::fromTheme("call-start"), "Fork from Here");
        }
    }

    QAction* selectedAction = menu.exec(chatTree->viewport()->mapToGlobal(pos));

    if (selectedAction == newChatAction) {
        bool ok;
        QString name = QInputDialog::getText(this, "New Chat", "Enter chat name/message:", QLineEdit::Normal, "", &ok);
        if (ok && !name.isEmpty() && currentDb) {
            int newId = currentDb->addMessage(0, name, "user");
            currentLastNodeId = newId;
            loadSession(0); // Refresh tree
        }
    } else if (item && selectedAction == copyAction) {
        QString fullText = item->text();
        int bracketIndex = fullText.indexOf("] ");
        if (bracketIndex != -1) {
            fullText = fullText.mid(bracketIndex + 2);
        }
        QGuiApplication::clipboard()->setText(fullText);
    } else if (item && selectedAction == pasteAction) {
        if (inputModeStack->currentIndex() == 0) {
            inputField->setText(inputField->text() + QGuiApplication::clipboard()->text());
        } else {
            multiLineInput->insertPlainText(QGuiApplication::clipboard()->text());
        }
    } else if (item && selectedAction == forkAction) {
        currentLastNodeId = item->data(Qt::UserRole).toInt();
        qDebug() << "Selected point for fork from tree view: " << currentLastNodeId;

        if (currentDb) {
            updateLinearChatView(currentLastNodeId, currentDb->getMessages());
            mainContentStack->setCurrentWidget(chatWindowView);
        }
    }
}

void MainWindow::onBookSelected(const QModelIndex& index) {
    QString fileName = bookList->item(index.row())->text();
    QString filePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/" + fileName;

    QString password = WalletManager::loadPassword(fileName);

    auto db = std::make_unique<BookDatabase>(filePath);
    if (!db->open(password)) {
        bool ok;
        password = QInputDialog::getText(this, "Unlock Book", "Enter password for " + fileName + ":", QLineEdit::Password, "", &ok);
        if (ok && db->open(password)) {
            if (!password.isEmpty()) {
                QMessageBox::StandardButton reply = QMessageBox::question(this, "Save Password", "Do you want to save this password to KWallet?", QMessageBox::Yes | QMessageBox::No);
                if (reply == QMessageBox::Yes) {
                    WalletManager::savePassword(fileName, password);
                }
            }
        } else {
            QMessageBox::warning(this, "Error", "Could not open book.");
            return;
        }
    }

    currentDb = std::move(db);

    // Add to open books tree
    QStandardItem* bookItem = new QStandardItem(QIcon::fromTheme("application-x-sqlite3"), fileName);
    bookItem->setData("book", Qt::UserRole + 1); // Type
    QStandardItem* chatsItem = new QStandardItem(QIcon::fromTheme("folder-open"), "Chats");
    chatsItem->setData("chats_folder", Qt::UserRole + 1);
    QStandardItem* docsItem = new QStandardItem(QIcon::fromTheme("folder-open"), "Documents");
    docsItem->setData("docs_folder", Qt::UserRole + 1);
    QStandardItem* notesItem = new QStandardItem(QIcon::fromTheme("folder-open"), "Notes");
    notesItem->setData("notes_folder", Qt::UserRole + 1);

    bookItem->appendRow(chatsItem);
    bookItem->appendRow(docsItem);
    bookItem->appendRow(notesItem);

    openBooksModel->appendRow(bookItem);
    openBooksTree->expandAll();

    // Remove from closed books list
    delete bookList->takeItem(index.row());

    loadSession(0); // For now, load all as one session
}

void MainWindow::loadSession(int rootId) {
    chatModel->clear();
    chatTextArea->clear();
    if (!currentDb) return;

    loadDocumentsAndNotes();

    QList<MessageNode> msgs = currentDb->getMessages();
    QStandardItem* rootItem = chatModel->invisibleRootItem();
    populateTree(rootItem, 0, msgs);
    chatTree->expandAll();

    if (!msgs.isEmpty()) {
        currentLastNodeId = msgs.last().id;
    } else {
        currentLastNodeId = 0;
    }

    updateLinearChatView(currentLastNodeId, msgs);
}

void MainWindow::getPathToRoot(int nodeId, const QList<MessageNode>& allMessages, QList<MessageNode>& path) {
    for (const auto& msg : allMessages) {
        if (msg.id == nodeId) {
            path.prepend(msg); // Prepend to build path from root downwards
            if (msg.parentId != 0) {
                getPathToRoot(msg.parentId, allMessages, path);
            }
            break;
        }
    }
}

void MainWindow::updateLinearChatView(int tailNodeId, const QList<MessageNode>& allMessages) {
    chatTextArea->clear();
    currentChatPath.clear();
    if (tailNodeId == 0 || allMessages.isEmpty()) return;

    getPathToRoot(tailNodeId, allMessages, currentChatPath);

    for (const auto& msg : currentChatPath) {
        QTextCursor cursor = chatTextArea->textCursor();
        cursor.movePosition(QTextCursor::End);
        chatTextArea->setTextCursor(cursor);
        if (msg.role == "user") {
            chatTextArea->insertHtml("<div style='font-weight: bold;'>[User]</div>");
        } else {
            chatTextArea->insertHtml("<div style='font-weight: bold; color:#00557f;'>[Assistant]</div>");
        }
        chatTextArea->insertPlainText(msg.content + "\n\n");
    }
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
    for (int i=0; i<parent->rowCount(); ++i) {
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

    if (m_isGenerating) {
        // User clicked Cancel
        m_isGenerating = false;
        ++m_generationId; // Invalidate the running lambda chunks
        chatTextArea->setReadOnly(false);
        inputModeStack->setEnabled(true);
        sendButton->setText("Send");
        statusBar->showMessage(tr("Generation cancelled."), 3000);
        return;
    }

    QString text;
    if (inputModeStack->currentIndex() == 0) {
        text = inputField->text();
        if (text.isEmpty()) return;
        inputField->clear();
    } else {
        text = multiLineInput->toPlainText();
        if (text.isEmpty()) return;
        multiLineInput->clear();
    }
    int parentId = currentLastNodeId;
    int newId = currentDb->addMessage(parentId, text, "user");

    // Add user message to in-memory path to keep parser synced
    MessageNode userNode;
    userNode.id = newId;
    userNode.parentId = parentId;
    userNode.role = "user";
    userNode.content = text;
    currentChatPath.append(userNode);

    QStandardItem* parentItem = chatModel->invisibleRootItem();
    if (parentId != 0) {
        QStandardItem* found = findItem(parentItem, parentId);
        if (found) parentItem = found;
    }

    QStandardItem* item = new QStandardItem(QString("[user] %1").arg(text));
    item->setData(newId, Qt::UserRole);
    if (parentItem) parentItem->appendRow(item);

    // Prepare AI response node
    int aiId = currentDb->addMessage(newId, "", "assistant");
    currentLastNodeId = aiId;

    // Add assistant message to in-memory path to keep parser synced
    MessageNode aiNode;
    aiNode.id = aiId;
    aiNode.parentId = newId;
    aiNode.role = "assistant";
    aiNode.content = "";
    currentChatPath.append(aiNode);

    QStandardItem* aiItem = new QStandardItem("[assistant] ");
    aiItem->setData(aiId, Qt::UserRole);
    item->appendRow(aiItem);
    chatTree->expandAll();

    // Since we manually appended to currentChatPath and the text area is already rendered,
    // we don't need to do a full clear-and-reload of updateLinearChatView right here.

    QString selectedModel = modelComboBox->currentData().toString();
    if (selectedModel.isEmpty()) {
        selectedModel = modelComboBox->currentText();
    }
    if (selectedModel.isEmpty()) selectedModel = "llama2";

    int currentGenId = ++m_generationId;
    m_isGenerating = true;
    chatTextArea->setReadOnly(true);
    inputModeStack->setEnabled(false);
    sendButton->setText("Cancel");

    // Add user message to UI immediately using basic mixed HTML/plain text
    QTextCursor cursor = chatTextArea->textCursor();
    cursor.movePosition(QTextCursor::End);
    chatTextArea->setTextCursor(cursor);
    chatTextArea->insertHtml("<div style='font-weight: bold;'>[User]</div>");
    chatTextArea->insertPlainText(text + "\n\n");

    ollamaClient.generate(selectedModel, text,
        [this, aiId, aiItem, currentGenId, isFirstChunk = true, fullResponse = QString()](const QString& chunk) mutable {
            if (currentGenId != m_generationId) return; // Ignore chunks if cancelled or superseded

            fullResponse += chunk;

            // Clean display: only show the first few words in the tree node
            QString displayContent = fullResponse;
            if (displayContent.contains('\n')) {
                displayContent = displayContent.left(displayContent.indexOf('\n')) + " ...";
            }
            aiItem->setText(QString("[assistant] %1").arg(displayContent));

            // Also update main view if it's the active tail
            if (currentLastNodeId == aiId) {
                QTextCursor cursor = chatTextArea->textCursor();
                cursor.movePosition(QTextCursor::End);
                chatTextArea->setTextCursor(cursor);

                if (isFirstChunk) {
                    chatTextArea->insertHtml("<div style='font-weight: bold; color:#00557f;'>[Assistant]</div>");
                    isFirstChunk = false;
                }

                chatTextArea->insertPlainText(chunk);

                // Keep the underlying DB synced with the exact full response
                currentDb->updateMessage(aiId, fullResponse);

                // Keep in-memory path synced for immediate "Save Edits" support
                if (!currentChatPath.isEmpty() && currentChatPath.last().id == aiId) {
                    currentChatPath.last().content = fullResponse;
                }
            }
        },
        [this, currentGenId](const QString& /*full*/) {
            if (currentGenId == m_generationId) {
                // Add concluding spacing
                QTextCursor cursor = chatTextArea->textCursor();
                cursor.movePosition(QTextCursor::End);
                chatTextArea->setTextCursor(cursor);
                chatTextArea->insertPlainText("\n\n");

                m_isGenerating = false;
                chatTextArea->setReadOnly(false); // Make editable again
                inputModeStack->setEnabled(true);
                sendButton->setText("Send");
            }
        },
        [this, aiItem, currentGenId](const QString& err) {
            if (currentGenId == m_generationId) {
                // It wasn't a deliberate cancel
                aiItem->setText(aiItem->text() + " [ERROR: " + err + "]");

                QTextCursor cursor = chatTextArea->textCursor();
                cursor.movePosition(QTextCursor::End);
                chatTextArea->setTextCursor(cursor);
                chatTextArea->insertHtml(QString("<br/><span style='color:red;'>[ERROR: %1]</span>\n\n").arg(err.toHtmlEscaped()));

                m_isGenerating = false;
                chatTextArea->setReadOnly(false); // Make editable again
                inputModeStack->setEnabled(true);
                sendButton->setText("Send");
            }
        }
    );
}

void MainWindow::onOllamaChunk(const QString& chunk) {}
void MainWindow::onOllamaComplete(const QString& fullResponse) {}
void MainWindow::onOllamaError(const QString& error) {}

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
        }
    }
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
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
                if (item && !item->parent()) { // Ensure it's a root item (book)
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
    if (currentDb && currentDb->isOpen()) {
        // Here we'd ideally check if the closed book is the currently active one
        // For now just close the active one to be safe
        currentDb->close();
        currentDb.reset();
        chatModel->clear();
        statusLabel->setText(tr("Ready"));
    }
}

void MainWindow::showBookContextMenu(const QPoint& pos) {
    QListWidgetItem* item = bookList->itemAt(pos);
    if (!item) return;

    QMenu menu(this);
    QAction* openAction = menu.addAction("Open");
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
    } else if (selectedAction == deleteAction) {
        QString fileName = item->text();
        QString filePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/" + fileName;
        QFile::remove(filePath);
        delete bookList->takeItem(bookList->row(item));
    } else if (selectedAction == favoriteAction) {
        if (isFavorite) {
            favorites.removeAll(fileName);
            item->setIcon(QIcon()); // remove icon
        } else {
            favorites.append(fileName);
            item->setIcon(QIcon::fromTheme("emblem-favorite"));
        }
        settings.setValue("favorites", favorites);
        // Re-sort or reload list to show favorites at top
        loadBooks();
    }
}

void MainWindow::onOpenBooksSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected) {
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

    QString type = item->data(Qt::UserRole + 1).toString();
    if (type == "book") {
        mainContentStack->setCurrentWidget(dbDirectView);
    } else if (type == "chats_folder") {
        mainContentStack->setCurrentWidget(chatsFolderView);
    } else if (type == "docs_folder") {
        mainContentStack->setCurrentWidget(documentsFolderView);
    } else if (type == "notes_folder") {
        mainContentStack->setCurrentWidget(notesFolderView);
    } else {
        // If it's none of the above, it's a specific chat node or otherwise unknown
        mainContentStack->setCurrentWidget(chatWindowView);
    }
}

void MainWindow::loadDocumentsAndNotes() {
    documentsModel->clear();
    notesModel->clear();
    if (!currentDb) return;

    QList<DocumentNode> docs = currentDb->getDocuments();
    for (const auto& doc : docs) {
        QStandardItem* item = new QStandardItem(doc.title);
        item->setData(doc.id, Qt::UserRole);
        documentsModel->appendRow(item);
    }

    QList<NoteNode> notes = currentDb->getNotes();
    for (const auto& note : notes) {
        QStandardItem* item = new QStandardItem(note.title);
        item->setData(note.id, Qt::UserRole);
        notesModel->appendRow(item);
    }
}

void MainWindow::showDocumentsContextMenu(const QPoint& pos) {
    QMenu menu(this);
    QAction* newDocAction = menu.addAction(QIcon::fromTheme("document-new"), "New Document");

    QAction* selectedAction = menu.exec(documentsFolderView->viewport()->mapToGlobal(pos));
    if (selectedAction == newDocAction) {
        bool ok;
        QString title = QInputDialog::getText(this, "New Document", "Enter document title:", QLineEdit::Normal, "", &ok);
        if (ok && !title.isEmpty() && currentDb) {
            currentDb->addDocument(0, title, "");
            loadDocumentsAndNotes();
        }
    }
}

void MainWindow::showNotesContextMenu(const QPoint& pos) {
    QMenu menu(this);
    QAction* newNoteAction = menu.addAction(QIcon::fromTheme("document-new"), "New Note");

    QAction* selectedAction = menu.exec(notesFolderView->viewport()->mapToGlobal(pos));
    if (selectedAction == newNoteAction) {
        bool ok;
        QString title = QInputDialog::getText(this, "New Note", "Enter note title:", QLineEdit::Normal, "", &ok);
        if (ok && !title.isEmpty() && currentDb) {
            currentDb->addNote(title, "");
            loadDocumentsAndNotes();
        }
    }
}

void MainWindow::showOpenBookContextMenu(const QPoint& pos) {
    QModelIndex index = openBooksTree->indexAt(pos);
    if (!index.isValid()) return;

    QStandardItem* item = openBooksModel->itemFromIndex(index);
    if (!item) return;

    QString type = item->data(Qt::UserRole + 1).toString();

    // Only show close for root items (books)
    if (type == "book") {
        QMenu menu(this);
        QAction* closeAction = menu.addAction("Close");

        QAction* selectedAction = menu.exec(openBooksTree->viewport()->mapToGlobal(pos));
        if (selectedAction == closeAction) {
            closeBook(item->text());
        }
    } else if (type == "chats_folder" || type == "docs_folder" || type == "notes_folder") {
        QMenu menu(this);
        QAction* newAction = menu.addAction("New Item");

        QAction* selectedAction = menu.exec(openBooksTree->viewport()->mapToGlobal(pos));
        if (selectedAction == newAction) {
            qDebug() << "New item for" << item->text();
            // Will be fully implemented later
        }
    }
}
