#include "MainWindow.h"

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
#include <QUrl>
#include <QVBoxLayout>
#include <QtGui/QAction>
#include <limits>

#include "ModelSelectionDialog.h"
#include "WalletManager.h"

MainWindow::MainWindow(QWidget* parent) : KXmlGuiWindow(parent), currentLastNodeId(0) {
    setupUi();
    setupWindow();
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
    openBooksTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    openBooksTree->setDropIndicatorShown(true);
    openBooksTree->installEventFilter(this);
    openBooksTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(openBooksTree, &QWidget::customContextMenuRequested, this, &MainWindow::showOpenBookContextMenu);

    connect(openBooksTree, &QTreeView::doubleClicked, this, [this](const QModelIndex& index) {
        QStandardItem* item = openBooksModel->itemFromIndex(index);
        if (item) {
            QString type = item->data(Qt::UserRole + 1).toString();
            if (type == "chats_folder") {
                mainRightStack->setCurrentWidget(chatContainerWidget);
            } else if (type == "book") {
                mainRightStack->setCurrentWidget(exploreListWidget);
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

    mainRightStack = new QStackedWidget(this);

    exploreListWidget = new QListWidget(this);
    exploreListWidget->setViewMode(QListView::IconMode);
    exploreListWidget->setIconSize(QSize(64, 64));
    exploreListWidget->setSpacing(20);
    exploreListWidget->setMovement(QListView::Static);
    exploreListWidget->setResizeMode(QListView::Adjust);

    QListWidgetItem* chatsItem = new QListWidgetItem(QIcon::fromTheme("folder-open"), "Chats");
    chatsItem->setData(Qt::UserRole, "chats");
    QListWidgetItem* docsItem = new QListWidgetItem(QIcon::fromTheme("folder-open"), "Documents");
    docsItem->setData(Qt::UserRole, "docs");
    QListWidgetItem* notesItem = new QListWidgetItem(QIcon::fromTheme("folder-open"), "Notes");
    notesItem->setData(Qt::UserRole, "notes");

    exploreListWidget->addItem(chatsItem);
    exploreListWidget->addItem(docsItem);
    exploreListWidget->addItem(notesItem);

    mainRightStack->addWidget(exploreListWidget);

    chatContainerWidget = new QWidget(this);
    QVBoxLayout* chatContainerLayout = new QVBoxLayout(chatContainerWidget);

    chatStackWidget = new QStackedWidget(this);

    // Linear View
    linearChatList = new QListWidget(this);
    linearChatList->setContextMenuPolicy(Qt::CustomContextMenu);
    linearChatList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(linearChatList, &QWidget::customContextMenuRequested, this, &MainWindow::showLinearChatContextMenu);
    chatStackWidget->addWidget(linearChatList);

    // Tree View
    chatTree = new QTreeView(this);
    chatTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(chatTree, &QWidget::customContextMenuRequested, this, &MainWindow::showChatTreeContextMenu);
    chatModel = new QStandardItemModel(this);
    chatTree->setModel(chatModel);
    chatTree->setHeaderHidden(true);
    chatTree->setEditTriggers(QAbstractItemView::DoubleClicked);
    chatStackWidget->addWidget(chatTree);

    chatContainerLayout->addWidget(chatStackWidget);

    inputField = new QLineEdit(this);
    sendButton = new QPushButton("Send", this);
    modelSelectButton = new QPushButton(QIcon::fromTheme("system-search"), "Select Model", this);

    QHBoxLayout* inputLayout = new QHBoxLayout();
    inputLayout->addWidget(modelSelectButton);
    inputLayout->addWidget(inputField);
    inputLayout->addWidget(sendButton);
    chatContainerLayout->addLayout(inputLayout);

    mainRightStack->addWidget(chatContainerWidget);

    splitter->addWidget(mainRightStack);

    // Initial sizes
    int totalWidth = width();
    int leftWidth = qMax(totalWidth * 20 / 100, 100);
    int rightWidth = totalWidth - leftWidth;
    splitter->setSizes(QList<int>() << leftWidth << rightWidth);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    QToolBar* toolbar = addToolBar("Main");
    QAction* newBookAction = new QAction(QIcon::fromTheme("document-new"), tr("New Book"), this);
    QAction* exportChatAction = new QAction(QIcon::fromTheme("document-export"), tr("Export Chat Session"), this);
    actionCollection()->addAction(QStringLiteral("export_chat_session"), exportChatAction);
    connect(exportChatAction, &QAction::triggered, this, &MainWindow::exportChatSession);

    QAction* toggleViewAction = new QAction(QIcon::fromTheme("view-split-left-right"), tr("Toggle Chat View"), this);
    actionCollection()->addAction(QStringLiteral("toggle_view"), toggleViewAction);
    toolbar->addAction(toggleViewAction);
    connect(toggleViewAction, &QAction::triggered, this, [this]() {
        if (chatStackWidget->currentIndex() == 0) {
            chatStackWidget->setCurrentIndex(1);
        } else {
            chatStackWidget->setCurrentIndex(0);
        }
    });
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
    connect(inputField, &QLineEdit::returnPressed, this, &MainWindow::onSendMessage);
    connect(chatModel, &QStandardItemModel::itemChanged, this, &MainWindow::onItemChanged);
    connect(chatTree->selectionModel(), &QItemSelectionModel::currentChanged, this, &MainWindow::onChatNodeSelected);

    connect(exploreListWidget, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (item->data(Qt::UserRole).toString() == "chats") {
            mainRightStack->setCurrentWidget(chatContainerWidget);
        }
    });

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

    QAction* settingsAction = new QAction(QIcon::fromTheme("preferences-system"), tr("Settings..."), this);
    actionCollection()->addAction(QStringLiteral("preferences"), settingsAction);
    connect(settingsAction, &QAction::triggered, this, &MainWindow::showSettingsDialog);

    QAction* quitAction = KStandardAction::quit(qApp, &QCoreApplication::quit, actionCollection());
    actionCollection()->addAction("quit", quitAction);

    QAction* aboutQtAction = new QAction(tr("About &Qt"), this);
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
    connect(dlg, &SettingsDialog::settingsApplied, this, [this]() { updateEndpointsList(); });
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
        QListWidgetItem* item = new QListWidgetItem(QIcon::fromTheme("emblem-favorite"), file.fileName());
        item->setToolTip(tr("Last modified: %1").arg(QLocale().toString(file.lastModified(), QLocale::ShortFormat)));
        bookList->addItem(item);
    }

    // Then add non-favorites
    for (const QFileInfo& file : otherList) {
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

void MainWindow::showLinearChatContextMenu(const QPoint& pos) {
    QListWidgetItem* item = linearChatList->itemAt(pos);
    if (!item) return;

    QMenu menu(this);
    QAction* copyAction = menu.addAction(QIcon::fromTheme("edit-copy"), "Copy Message");
    QAction* pasteAction = menu.addAction(QIcon::fromTheme("edit-paste"), "Paste to Input");
    menu.addSeparator();
    QAction* forkAction = menu.addAction(QIcon::fromTheme("call-start"), "Fork from Here");
    menu.addSeparator();
    QAction* exportAction = menu.addAction(QIcon::fromTheme("document-export"), "Export Chat Session");

    QAction* selectedAction = menu.exec(linearChatList->viewport()->mapToGlobal(pos));
    if (selectedAction == copyAction) {
        QString fullText = item->text();
        // Remove the role tag e.g., "[user] " or "[assistant] "
        int bracketIndex = fullText.indexOf("] ");
        if (bracketIndex != -1) {
            fullText = fullText.mid(bracketIndex + 2);
        }
        QGuiApplication::clipboard()->setText(fullText);
    } else if (selectedAction == pasteAction) {
        inputField->setText(inputField->text() + QGuiApplication::clipboard()->text());
    } else if (selectedAction == forkAction) {
        currentLastNodeId = item->data(Qt::UserRole).toInt();
        qDebug() << "Selected point for fork from linear view: " << currentLastNodeId;

        // Highlight it in the tree if we switch views
        QStandardItem* foundItem = findItem(chatModel->invisibleRootItem(), currentLastNodeId);
        if (foundItem) {
            chatTree->setCurrentIndex(foundItem->index());
        }

        if (currentDb) {
            updateLinearChatView(currentLastNodeId, currentDb->getMessages());
        }
    } else if (selectedAction == exportAction) {
        exportChatSession();
    }
}

void MainWindow::showChatTreeContextMenu(const QPoint& pos) {
    QModelIndex index = chatTree->indexAt(pos);
    if (!index.isValid()) return;

    QStandardItem* item = chatModel->itemFromIndex(index);
    if (!item) return;

    QMenu menu(this);
    QAction* copyAction = menu.addAction(QIcon::fromTheme("edit-copy"), "Copy Message");
    QAction* pasteAction = menu.addAction(QIcon::fromTheme("edit-paste"), "Paste to Input");
    menu.addSeparator();
    QAction* forkAction = menu.addAction(QIcon::fromTheme("call-start"), "Fork from Here");
    menu.addSeparator();
    QAction* exportAction = menu.addAction(QIcon::fromTheme("document-export"), "Export Chat Session");

    QAction* selectedAction = menu.exec(chatTree->viewport()->mapToGlobal(pos));
    if (selectedAction == copyAction) {
        QString fullText = item->text();
        int bracketIndex = fullText.indexOf("] ");
        if (bracketIndex != -1) {
            fullText = fullText.mid(bracketIndex + 2);
        }
        QGuiApplication::clipboard()->setText(fullText);
    } else if (selectedAction == pasteAction) {
        inputField->setText(inputField->text() + QGuiApplication::clipboard()->text());
    } else if (selectedAction == forkAction) {
        currentLastNodeId = item->data(Qt::UserRole).toInt();
        qDebug() << "Selected point for fork from tree view: " << currentLastNodeId;

        if (currentDb) {
            updateLinearChatView(currentLastNodeId, currentDb->getMessages());
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

    currentDb = std::move(db);

    // Add to open books tree
    QStandardItem* bookItem = new QStandardItem(QIcon::fromTheme("application-x-sqlite3"), fileName);
    bookItem->setData("book", Qt::UserRole + 1);  // Type
    QStandardItem* chatsItem = new QStandardItem(QIcon::fromTheme("folder-open"), "Chats");
    chatsItem->setData("chats_folder", Qt::UserRole + 1);

    // Add a dummy chat session item for testing export/import menu options
    QStandardItem* defaultSessionItem = new QStandardItem(QIcon::fromTheme("view-list-tree"), "Default Session");
    defaultSessionItem->setData("chat_session", Qt::UserRole + 1);
    chatsItem->appendRow(defaultSessionItem);

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

    loadSession(0);  // For now, load all as one session

    mainRightStack->setCurrentWidget(exploreListWidget);
}

void MainWindow::loadSession(int rootId) {
    chatModel->clear();
    linearChatList->clear();
    if (!currentDb) return;

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
            path.prepend(msg);  // Prepend to build path from root downwards
            if (msg.parentId != 0) {
                getPathToRoot(msg.parentId, allMessages, path);
            }
            break;
        }
    }
}

void MainWindow::updateLinearChatView(int tailNodeId, const QList<MessageNode>& allMessages) {
    linearChatList->clear();
    if (tailNodeId == 0 || allMessages.isEmpty()) return;

    QList<MessageNode> path;
    getPathToRoot(tailNodeId, allMessages, path);

    for (const auto& msg : path) {
        QListWidgetItem* item = new QListWidgetItem(QString("[%1] %2").arg(msg.role, msg.content));
        item->setData(Qt::UserRole, msg.id);
        linearChatList->addItem(item);
    }
    linearChatList->scrollToBottom();
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
    QString text = inputField->text();
    if (text.isEmpty()) return;

    inputField->clear();
    int parentId = currentLastNodeId;
    int newId = currentDb->addMessage(parentId, text, "user");

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
    QStandardItem* aiItem = new QStandardItem("[assistant] ");
    aiItem->setData(aiId, Qt::UserRole);
    item->appendRow(aiItem);
    chatTree->expandAll();

    updateLinearChatView(currentLastNodeId, currentDb->getMessages());

    QString selectedModel = m_selectedModel;
    if (selectedModel.isEmpty()) selectedModel = "llama2";

    ollamaClient.generate(
        selectedModel, text,
        [this, aiId, aiItem](const QString& chunk) {
            QString currentText = aiItem->text() + chunk;
            aiItem->setText(currentText);

            // Also update linear view if it's the active tail
            if (currentLastNodeId == aiId && linearChatList->count() > 0) {
                QListWidgetItem* lastLinearItem = linearChatList->item(linearChatList->count() - 1);
                if (lastLinearItem && lastLinearItem->data(Qt::UserRole).toInt() == aiId) {
                    lastLinearItem->setText(currentText);
                }
            }
        },
        [this, aiId, aiItem](const QString& /*full*/) {
            QString content = aiItem->text().mid(12);  // remove "[assistant] "
            currentDb->updateMessage(aiId, content);
        },
        [this, aiItem](const QString& err) { aiItem->setText(aiItem->text() + " [ERROR: " + err + "]"); });
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
    if (currentDb && currentDb->isOpen()) {
        // Here we'd ideally check if the closed book is the currently active one
        // For now just close the active one to be safe
        currentDb->close();
        currentDb.reset();
        chatModel->clear();
        statusLabel->setText(tr("Ready"));

        mainRightStack->setCurrentWidget(exploreListWidget);
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

        QAction* importAction = nullptr;
        if (type == "chats_folder") {
            importAction = menu.addAction("Import Chat Session");
        }

        QAction* selectedAction = menu.exec(openBooksTree->viewport()->mapToGlobal(pos));
        if (selectedAction == newAction) {
            qDebug() << "New item for" << item->text();
            // Will be fully implemented later
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
