#include "MainWindow.h"

#include <KActionCollection>
#include <KActionMenu>
#include <KStandardAction>
#include <KToolBar>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QGroupBox>
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
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStyle>
#include <QTextBlock>
#include <QUrl>
#include <QVBoxLayout>
#include <QtGui/QAction>
#include <limits>

#include "AIOperationsDialog.h"
#include "AiActionDialog.h"
#include "ChatSettingsDialog.h"
#include "DatabaseSettingsDialog.h"
#include "DocumentEditWindow.h"
#include "DocumentHistoryDialog.h"
#include "DocumentReviewDialog.h"
#include "ModelSelectionDialog.h"
#include "NotificationDelegate.h"
#include "QueueManager.h"
#include "QueueWindow.h"
#include "WalletManager.h"

CustomItemModel::CustomItemModel(QObject* parent) : QStandardItemModel(parent), m_mainWindow(nullptr) {}

/** * @brief Executes logic for mimeTypes. This function manages component initialization and handles state transitions
 * for the UI. *  * This function is an integral component of the MainWindow class structure. * It ensures that side
 * effects map accurately to internal application models. */
QStringList CustomItemModel::mimeTypes() const {
    QStringList types = QStandardItemModel::mimeTypes();
    if (!types.contains("text/uri-list")) {
        types << "text/uri-list";
    }
    return types;
}

/** * @brief Executes logic for mimeData. This function manages component initialization and handles state transitions
 * for the UI. *  * This function is an integral component of the MainWindow class structure. * It ensures that side
 * effects map accurately to internal application models. */
QMimeData* CustomItemModel::mimeData(const QModelIndexList& indexes) const {
    QMimeData* data = QStandardItemModel::mimeData(indexes);
    if (!m_mainWindow || indexes.isEmpty()) return data;

    QList<QUrl> urls;
    for (const QModelIndex& index : indexes) {
        QStandardItem* item = itemFromIndex(index);
        if (item && item->text() != "..") {
            QString type = item->data(Qt::UserRole + 1).toString();
            int id = item->data(Qt::UserRole).toInt();

            if (type == "document" || type == "note") {
                QString title, content;
                m_mainWindow->getDocumentContent(id, type, title, content);

                if (!content.isEmpty() || !title.isEmpty()) {
                    if (title.isEmpty()) title = "export";
                    if (!title.endsWith(".md")) title += ".md";

                    title = title.replace("/", "_").replace("\\", "_");
                    QString tempPath = QDir::tempPath() + "/" + title;
                    QFile tempFile(tempPath);
                    if (tempFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                        QTextStream out(&tempFile);
                        out << content;
                        tempFile.close();
                        urls.append(QUrl::fromLocalFile(tempPath));
                    }
                }
            }
        }
    }

    if (!urls.isEmpty()) {
        data->setUrls(urls);
        // Fallback for some managers
        if (urls.size() == 1 && data->text().isEmpty()) {
            QFile f(urls.first().toLocalFile());
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                data->setText(f.readAll());
                f.close();
            }
        }
    }
    return data;
}

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

/** * @brief Handles the application close event, safely persisting unsaved state. *  * This function is an integral
 * component of the MainWindow class structure. * It ensures that side effects map accurately to internal application
 * models. */
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

/** * @brief Sets up the user interface elements, connecting signals and slots for widgets. *  * This function is an
 * integral component of the MainWindow class structure. * It ensures that side effects map accurately to internal
 * application models. */
void MainWindow::setupUi() {
    splitter = new QSplitter(this);
    setCentralWidget(splitter);

    leftSplitter = new QSplitter(Qt::Vertical, this);

    openBooksTree = new QTreeView(this);
    openBooksTree->setMinimumSize(0, 0);
    openBooksModel = new CustomItemModel(this);
    openBooksModel->setMainWindow(this);
    openBooksTree->setModel(openBooksModel);
    openBooksTree->setHeaderHidden(true);
    openBooksTree->setAcceptDrops(true);
    openBooksTree->setDragEnabled(true);
    openBooksTree->setDragDropMode(QAbstractItemView::DragDrop);
    openBooksTree->setDefaultDropAction(Qt::MoveAction);
    openBooksTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    openBooksTree->setDropIndicatorShown(true);
    openBooksTree->installEventFilter(this);
    openBooksTree->viewport()->installEventFilter(this);
    openBooksTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(openBooksTree, &QWidget::customContextMenuRequested, this, &MainWindow::showOpenBookContextMenu);

    connect(openBooksTree, &QTreeView::doubleClicked, this, &MainWindow::onOpenBooksTreeDoubleClicked);

    bookList = new QListWidget(this);
    bookList->setMinimumSize(0, 0);
    bookList->setAcceptDrops(true);
    bookList->setDragEnabled(true);
    bookList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    bookList->setDragDropMode(QAbstractItemView::DragDrop);
    bookList->installEventFilter(this);
    bookList->viewport()->installEventFilter(this);
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
    vfsModel = new CustomItemModel(this);
    vfsModel->setMainWindow(this);
    vfsExplorer->setModel(vfsModel);
    vfsExplorer->setViewMode(QListView::IconMode);
    vfsExplorer->setDropIndicatorShown(true);
    vfsExplorer->setDragDropOverwriteMode(true);
    vfsExplorer->setAcceptDrops(true);
    vfsExplorer->setDragEnabled(true);
    vfsExplorer->setDragDropMode(QAbstractItemView::DragDrop);
    vfsExplorer->setDefaultDropAction(Qt::MoveAction);
    vfsExplorer->setEditTriggers(QAbstractItemView::NoEditTriggers);
    vfsExplorer->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(vfsExplorer, &QWidget::customContextMenuRequested, this, &MainWindow::showVfsContextMenu);
    vfsExplorer->installEventFilter(this);
    vfsExplorer->viewport()->installEventFilter(this);
    mainContentStack->addWidget(vfsExplorer);

    connect(vfsExplorer, &QListView::doubleClicked, this, &MainWindow::onVfsExplorerDoubleClicked);

    chatModel = new QStandardItemModel(this);
    chatTree = openBooksTree;

    docContainer = new QWidget(this);
    QVBoxLayout* docLayout = new QVBoxLayout(docContainer);
    docLayout->setContentsMargins(0, 0, 0, 0);

    QToolBar* docToolbar = new QToolBar(this);
    // saveDocBtn is now unused in the main view, replaced by editDocBtn
    editDocBtn = new QPushButton(QIcon::fromTheme("document-edit"), "Edit Document", this);
    saveDocBtn = new QPushButton(QIcon::fromTheme("document-save"), "Save Document", this);
    saveDocBtn->hide();  // kept just in case but we don't need it on this toolbar

    QPushButton* backToDocsBtn = new QPushButton(QIcon::fromTheme("go-previous"), "Back to Documents", this);
    previewDocBtn = new QPushButton(QIcon::fromTheme("view-preview"), "As Markdown", this);
    previewDocBtn->setCheckable(true);

    QPushButton* aiOperationsBtn = new QPushButton(QIcon::fromTheme("tools-wizard"), "AI Operations", this);
    connect(aiOperationsBtn, &QPushButton::clicked, this, &MainWindow::onDocumentAIOperations);

    QPushButton* docHistoryBtn = new QPushButton(QIcon::fromTheme("view-history"), "History", this);
    connect(docHistoryBtn, &QPushButton::clicked, this, &MainWindow::onDocumentHistory);

    docToolbar->addWidget(backToDocsBtn);
    docToolbar->addWidget(editDocBtn);
    docToolbar->addWidget(previewDocBtn);
    docToolbar->addWidget(docHistoryBtn);
    docToolbar->addWidget(aiOperationsBtn);
    docLayout->addWidget(docToolbar);

    documentStack = new QStackedWidget(this);
    documentEditorView = new QTextEdit(this);
    documentEditorView->setReadOnly(true);  // User requirement: non-editable
    documentPreviewView = new QTextBrowser(this);

    documentStack->addWidget(documentEditorView);
    documentStack->addWidget(documentPreviewView);

    docLayout->addWidget(documentStack);
    mainContentStack->addWidget(docContainer);

    connect(editDocBtn, &QPushButton::clicked, this, &MainWindow::onEditDocument);

    connect(previewDocBtn, &QPushButton::toggled, this, [this](bool checked) {
        if (checked) {
            documentPreviewView->setMarkdown(documentEditorView->toPlainText());
            documentStack->setCurrentWidget(documentPreviewView);
            previewDocBtn->setText("As Text");
        } else {
            documentStack->setCurrentWidget(documentEditorView);
            previewDocBtn->setText("As Markdown");
        }
    });

    noteContainer = new QWidget(this);
    QVBoxLayout* noteLayout = new QVBoxLayout(noteContainer);
    noteLayout->setContentsMargins(0, 0, 0, 0);

    QToolBar* noteToolbar = new QToolBar(this);
    saveNoteBtn = new QPushButton(QIcon::fromTheme("document-save"), "Save Note", this);
    QPushButton* backToNotesBtn = new QPushButton(QIcon::fromTheme("go-previous"), "Back to Notes", this);
    noteToolbar->addWidget(backToNotesBtn);
    noteToolbar->addWidget(saveNoteBtn);
    noteLayout->addWidget(noteToolbar);

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

    noteLayout->addWidget(noteEditorView);
    mainContentStack->addWidget(noteContainer);

    connect(backToDocsBtn, &QPushButton::clicked, this, [this]() {
        mainContentStack->setCurrentWidget(vfsExplorer);
        QModelIndex currentIdx = openBooksTree->currentIndex();
        if (currentIdx.isValid()) {
            QStandardItem* currentItem = openBooksModel->itemFromIndex(currentIdx);
            if (currentItem && currentItem->parent()) {
                openBooksTree->setCurrentIndex(currentItem->parent()->index());
                openBooksTree->selectionModel()->select(
                    currentItem->parent()->index(), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Current);
            }
        }
    });

    connect(backToNotesBtn, &QPushButton::clicked, this, [this]() {
        mainContentStack->setCurrentWidget(vfsExplorer);
        QModelIndex currentIdx = openBooksTree->currentIndex();
        if (currentIdx.isValid()) {
            QStandardItem* currentItem = openBooksModel->itemFromIndex(currentIdx);
            if (currentItem && currentItem->parent()) {
                openBooksTree->setCurrentIndex(currentItem->parent()->index());
                openBooksTree->selectionModel()->select(
                    currentItem->parent()->index(), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Current);
            }
        }
    });

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

        QString textToSave = (mainContentStack->currentWidget() == docContainer) ? documentEditorView->toPlainText()
                                                                                 : noteEditorView->toPlainText();
        if (currentAutoDraftId == 0) {
            currentAutoDraftId = currentDb->addDraft(0, title, textToSave);
        } else {
            currentDb->updateDraft(currentAutoDraftId, title, textToSave);
        }
    });

    connect(saveDocBtn, &QPushButton::clicked, this, &MainWindow::onSaveDocBtnClicked);

    connect(saveNoteBtn, &QPushButton::clicked, this, &MainWindow::onSaveNoteBtnClicked);

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
    connect(chatTextArea, &QWidget::customContextMenuRequested, this, &MainWindow::onChatTextAreaContextMenu);
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

    connect(chatForkExplorer, &QListView::doubleClicked, this, &MainWindow::onChatForkExplorerDoubleClicked);

    chatForkExplorer->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(chatForkExplorer, &QWidget::customContextMenuRequested, this, &MainWindow::onChatForkExplorerContextMenu);

    chatSplitter->addWidget(chatForkExplorer);
    chatLayout->addWidget(chatSplitter);

    QHBoxLayout* inputLayout = new QHBoxLayout();

    modelSelectButton = new QPushButton(QIcon::fromTheme("system-search"), "Select Model", this);
    toggleInputModeBtn = new QPushButton(QIcon::fromTheme("view-list-details"), "", this);
    toggleInputModeBtn->setToolTip(tr("Toggle Multi-line Input"));
    toggleInputModeBtn->setCheckable(true);

    inputField = new ChatInputWidget(this);

    multiLineInput = new QTextEdit(this);
    multiLineInput->setMaximumHeight(100);
    multiLineInput->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    multiLineInput->hide();

    inputLayout->addWidget(inputField);
    inputLayout->addWidget(multiLineInput);

    discardChangesBtn = new QPushButton("Discard Changes", this);
    discardChangesBtn->setToolTip(tr("Discard any pending edits to history."));
    discardChangesBtn->hide();  // Initially hidden until an edit happens
    inputLayout->addWidget(discardChangesBtn);

    dismissDraftBtn = new QPushButton("Dismiss Draft", this);
    dismissDraftBtn->setToolTip(tr("Dismiss the current draft."));
    dismissDraftBtn->hide();  // Initially hidden until a draft exists
    inputLayout->addWidget(dismissDraftBtn);

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

    QHBoxLayout* topBtnLayout = new QHBoxLayout();
    topBtnLayout->addWidget(sendButton);
    topBtnLayout->addWidget(inputSettingsButton);
    inputLayout->addLayout(topBtnLayout);
    inputLayout->setAlignment(topBtnLayout, Qt::AlignTop);

    chatInputLayout->addLayout(inputLayout);

    connect(discardChangesBtn, &QPushButton::clicked, this, &MainWindow::onDiscardChanges);
    connect(dismissDraftBtn, &QPushButton::clicked, this, &MainWindow::onDismissDraft);

    mainContentStack->addWidget(chatWindowView);

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
    connect(openBooksModel, &QStandardItemModel::dataChanged, this,
            [this](const QModelIndex& topLeft, const QModelIndex& bottomRight) {
                if (mainContentStack->currentWidget() == vfsExplorer) {
                    QModelIndex currentIdx = openBooksTree->currentIndex();
                    if (topLeft.parent() == currentIdx) {
                        refreshVfsExplorer();
                    }
                }
            });
    connect(openBooksModel, &QStandardItemModel::rowsInserted, this,
            [this](const QModelIndex& parent, int first, int last) {
                if (mainContentStack->currentWidget() == vfsExplorer) {
                    QModelIndex currentIdx = openBooksTree->currentIndex();
                    if (parent == currentIdx) {
                        refreshVfsExplorer();
                    }
                }
            });
    connect(openBooksModel, &QStandardItemModel::rowsRemoved, this,
            [this](const QModelIndex& parent, int first, int last) {
                if (mainContentStack->currentWidget() == vfsExplorer) {
                    QModelIndex currentIdx = openBooksTree->currentIndex();
                    if (parent == currentIdx) {
                        refreshVfsExplorer();
                    }
                }
            });
    connect(toggleInputModeBtn, &QPushButton::toggled, this, [this](bool checked) {
        if (checked) {
            inputField->hide();
            inputField->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
            multiLineInput->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
            multiLineInput->show();
        } else {
            multiLineInput->hide();
            multiLineInput->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
            inputField->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
            inputField->show();
        }
    });

    connect(mainContentStack, &QStackedWidget::currentChanged, this,
            [this](int index) { toggleInputModeBtn->setVisible(mainContentStack->widget(index) == chatWindowView); });
    connect(toggleInputModeBtn, &QPushButton::toggled, this, [this](bool checked) {
        chatStackWidget->setCurrentIndex(checked ? 1 : 0);
        if (currentDb && currentDb->isOpen()) {
            QString newMultiLine = checked ? "Multi Line" : "Single Line";
            if (currentLastNodeId != 0) {
                ChatNode cnMulti = currentDb->getChat(currentLastNodeId);
                cnMulti.multiLine = newMultiLine;
                currentDb->updateChat(cnMulti);
            } else {
                m_newChatMultiLine = newMultiLine;
            }
        }
    });

    // Initial sizes
    int totalWidth = width();
    int leftWidth = qMax(totalWidth * 20 / 100, 100);
    int rightWidth = totalWidth - leftWidth;
    splitter->setSizes(QList<int>() << leftWidth << rightWidth);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    QAction* newBookAction = new QAction(QIcon::fromTheme("document-new"), tr("New Book"), this);
    QAction* exportChatAction = new QAction(QIcon::fromTheme("document-export"), tr("Export Chat Session"), this);
    actionCollection()->addAction(QStringLiteral("export_chat_session"), exportChatAction);
    connect(exportChatAction, &QAction::triggered, this, &MainWindow::exportChatSession);

    actionCollection()->addAction(QStringLiteral("new_book"), newBookAction);

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
    actionCollection()->addAction(QStringLiteral("create_folder_menu"), createFolderMenuAction);
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

    endpointComboBox = new QComboBox(this);
    endpointComboBox->setMinimumWidth(150);

    connectionStatusLabel = new QLabel(this);
    connectionStatusLabel->setMargin(4);
    connectionStatusLabel->setCursor(Qt::PointingHandCursor);
    connectionStatusLabel->installEventFilter(this);
    onConnectionStatusChanged(false);  // Initially disconnected

    connect(endpointComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::onActiveEndpointChanged);
    connect(&ollamaClient, &OllamaClient::connectionStatusChanged, this, &MainWindow::onConnectionStatusChanged);
    connect(&ollamaClient, &OllamaClient::generationMetrics, this, &MainWindow::onGenerationMetrics);

    connect(newBookAction, &QAction::triggered, this, &MainWindow::onCreateBook);
    connect(bookList, &QListWidget::doubleClicked, this,
            &MainWindow::onBookSelected);  // Open book from list via double click
    connect(sendButton, &QPushButton::clicked, this, &MainWindow::onSendMessage);
    connect(inputField, &ChatInputWidget::returnPressed, this, &MainWindow::onSendMessage);

    QTimer* draftDebounceTimer = new QTimer(this);
    draftDebounceTimer->setSingleShot(true);
    draftDebounceTimer->setInterval(1000);  // 1 second debounce

    auto saveDraftFunc = [this]() {
        if (currentDb && currentDb->isOpen()) {
            QString textToSave =
                !toggleInputModeBtn->isChecked() ? inputField->toPlainText() : multiLineInput->toPlainText();

            if (m_activeDraftNodeId != 0) {
                ChatNode cnDraft = currentDb->getChat(m_activeDraftNodeId);
                if (cnDraft.version == m_activeDraftVersion) {
                    cnDraft.draftPrompt = textToSave;
                    if (currentDb->updateChat(cnDraft)) {
                        m_activeDraftVersion++;  // keep in sync with optimistic lock
                    }
                }
            } else {
                m_newChatDraftPrompt = textToSave;
            }
        }
    };

    connect(draftDebounceTimer, &QTimer::timeout, this, saveDraftFunc);
    connect(inputField, &ChatInputWidget::textChanged, this, [this, draftDebounceTimer]() {
        draftDebounceTimer->start();
        if (inputField->toPlainText().isEmpty()) dismissDraftBtn->hide(); else dismissDraftBtn->show();
    });
    connect(multiLineInput, &QTextEdit::textChanged, this, [this, draftDebounceTimer]() {
        draftDebounceTimer->start();
        if (multiLineInput->toPlainText().isEmpty()) dismissDraftBtn->hide(); else dismissDraftBtn->show();
    });

    connect(chatModel, &QStandardItemModel::itemChanged, this, &MainWindow::onItemChanged);
    connect(chatTree->selectionModel(), &QItemSelectionModel::currentChanged, this, &MainWindow::onChatNodeSelected);

    QSettings settings;
    m_availableModels = settings.value("cachedModels").toStringList();

    connect(&ollamaClient, &OllamaClient::modelListUpdated, this, [this](const QStringList& models) {
        QSettings settings;
        if (!models.isEmpty()) {
            m_availableModels = models;
            settings.setValue("cachedModels", m_availableModels);
        } else if (m_availableModels.isEmpty()) {
            m_availableModels = settings.value("cachedModels").toStringList();
            if (m_availableModels.isEmpty()) {
                m_availableModels.append("llama2");  // fallback
            }
        }
        if (m_selectedModel.isEmpty() && !m_availableModels.isEmpty()) {
            m_selectedModel = m_availableModels.first();
            modelSelectButton->setText(m_selectedModel);
            modelLabel->setText(tr("Model: %1 (Default)").arg(m_selectedModel));
        }
    });

    connect(modelSelectButton, &QPushButton::clicked, this, [this]() {
        ModelSelectionDialog dlg(m_availableModels, this);
        if (dlg.exec() == QDialog::Accepted) {
            QString selected = dlg.selectedModel();
            if (!selected.isEmpty()) {
                if (currentDb && currentDb->isOpen()) {
                    if (currentLastNodeId != 0) {
                        ChatNode cnMod = currentDb->getChat(currentLastNodeId);
                        cnMod.model = selected;
                        currentDb->updateChat(cnMod);
                    } else {
                        m_newChatModel = selected;
                    }
                }
                m_selectedModel = selected;
                modelSelectButton->setText(m_selectedModel);
                modelLabel->setText(tr("Model: %1 (User Switch)").arg(m_selectedModel));
                updateInputBehavior();
            }
        }
    });

    connect(&ollamaClient, &OllamaClient::pullProgressUpdated, this, &MainWindow::onPullProgressUpdated);
    connect(&ollamaClient, &OllamaClient::pullFinished, this, &MainWindow::onPullFinished);

    QAction* modelExplorerAction = new QAction(QIcon::fromTheme("server-database"), tr("Model Explorer"), this);
    actionCollection()->addAction(QStringLiteral("model_explorer"), modelExplorerAction);
    connect(modelExplorerAction, &QAction::triggered, this, &MainWindow::showModelExplorer);

    QAction* settingsAction = KStandardAction::preferences(this, &MainWindow::showSettingsDialog, actionCollection());

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

    QAction* showQueueAction = new QAction(QIcon::fromTheme("view-list-details"), tr("Show Queue"), this);
    connect(showQueueAction, &QAction::triggered, this, &MainWindow::showQueueWindow);
    actionCollection()->addAction(QStringLiteral("show_queue"), showQueueAction);

    QAction* showSpyAction = new QAction(QIcon::fromTheme("view-preview"), tr("Show Global Spy"), this);
    connect(showSpyAction, &QAction::triggered, this, &MainWindow::showSpyWindow);
    actionCollection()->addAction(QStringLiteral("show_spy"), showSpyAction);

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

    KToolBar* mainToolBar = toolBar("mainToolBar");
    if (mainToolBar) {
        // Endpoints in toolbar
        QWidget* spacer = new QWidget(this);
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        mainToolBar->addWidget(spacer);

        QLabel* endpointLabel = new QLabel(tr("Endpoint: "), this);
        mainToolBar->addWidget(endpointLabel);

        mainToolBar->addWidget(endpointComboBox);
        mainToolBar->addWidget(connectionStatusLabel);
    }

    // Status Bar
    statusBar = new QStatusBar(this);
    setStatusBar(statusBar);

    statusLabel = new QLabel(tr("Ready"), this);
    statusBar->addWidget(statusLabel);

    statusBar->addPermanentWidget(modelSelectButton);
    statusBar->addPermanentWidget(toggleInputModeBtn);

    // Initially hide toggleInputModeBtn since emptyView is default
    toggleInputModeBtn->hide();

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

    settingsStatusBarBtn = new QToolButton(this);
    settingsStatusBarBtn->setIcon(QIcon::fromTheme("configure"));
    settingsStatusBarBtn->setToolTip(tr("Settings"));
    connect(settingsStatusBarBtn, &QToolButton::clicked, this, &MainWindow::showSettingsDialog);
    statusBar->addPermanentWidget(settingsStatusBarBtn);

    updateEndpointsList();
    loadBooks();
}

/** * @brief Refreshes the combobox containing remote Ollama API endpoint targets. *  * This function is an integral
 * component of the MainWindow class structure. * It ensures that side effects map accurately to internal application
 * models. */
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

/** * @brief Callback when the user selects a new LLM endpoint target. *  * This function is an integral component of
 * the MainWindow class structure. * It ensures that side effects map accurately to internal application models. */
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

/** * @brief Callback triggered when the background OllamaClient signals a status shift. *  * This function is an
 * integral component of the MainWindow class structure. * It ensures that side effects map accurately to internal
 * application models. */
void MainWindow::onConnectionStatusChanged(bool isOk) {
    if (isOk) {
        connectionStatusLabel->setText("🟢");
        connectionStatusLabel->setToolTip(tr("Connected"));
    } else {
        connectionStatusLabel->setText("🔴");
        connectionStatusLabel->setToolTip(tr("Disconnected / Error"));
    }
}

void MainWindow::onGenerationMetrics(double tokensPerSecond) {
    statusLabel->setText(QString("Generation Speed: %1 tokens/sec").arg(tokensPerSecond, 0, 'f', 2));
}

/** * @brief Spawns the global application settings dialog modal. *  * This function is an integral component of the
 * MainWindow class structure. * It ensures that side effects map accurately to internal application models. */
void MainWindow::showSettingsDialog() {
    SettingsDialog* dlg = new SettingsDialog(this);
    connect(dlg, &SettingsDialog::settingsApplied, this, [this]() {
        updateEndpointsList();
        updateInputBehavior();
    });
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

/** * @brief Displays the pop-up configuration menu for chat input behaviors. *  * This function is an integral
 * component of the MainWindow class structure. * It ensures that side effects map accurately to internal application
 * models. */
void MainWindow::showInputSettingsMenu() {
    QMenu menu(this);

    QAction* toggleMultiLineAction = menu.addAction(tr("Toggle Multi-line Mode"));
    toggleMultiLineAction->setCheckable(true);
    toggleMultiLineAction->setChecked(toggleInputModeBtn->isChecked());
    connect(toggleMultiLineAction, &QAction::triggered, this,
            [this](bool checked) { toggleInputModeBtn->setChecked(checked); });

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

    if (currentDb && currentDb->isOpen()) {
        if (currentLastNodeId != 0) {
            currentChatSetting = currentDb->getChat(currentLastNodeId).sendBehavior;
        } else {
            currentChatSetting = m_newChatSendBehavior;
        }
    } else if (!currentDb || !currentDb->isOpen()) {
        chatMenu->setEnabled(false);
    }

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

    if (currentChatSetting == "EnterToSend")
        cEnter->setChecked(true);
    else if (currentChatSetting == "CtrlEnterToSend")
        cCtrl->setChecked(true);
    else
        cDef->setChecked(true);

    connect(chatGroup, &QActionGroup::triggered, this, [this](QAction* action) {
        if (currentDb && currentDb->isOpen()) {
            QString newBehavior = action->data().toString();
            if (currentLastNodeId == 0) {
                m_newChatSendBehavior = newBehavior;
            } else {
                ChatNode cnBeh = currentDb->getChat(currentLastNodeId);
                cnBeh.sendBehavior = newBehavior;
                currentDb->updateChat(cnBeh);
            }
            updateInputBehavior();
        }
    });

    QMenu* chatModelMenu = menu.addMenu(tr("Chat Model"));
    QActionGroup* chatModelGroup = new QActionGroup(&menu);
    QAction* mDef = chatModelMenu->addAction(tr("Use Parent Default"));
    mDef->setData("default");
    mDef->setCheckable(true);
    chatModelGroup->addAction(mDef);

    int modelCount = 0;
    QString endpointName = endpointComboBox ? endpointComboBox->currentText() : "Default";
    for (const QString& modelName : m_availableModels) {
        if (modelCount >= 10) break;
        QString displayString = QString("%1's %2").arg(endpointName, modelName);
        QAction* mAction = chatModelMenu->addAction(displayString);
        mAction->setData(modelName);
        mAction->setCheckable(true);
        chatModelGroup->addAction(mAction);
        modelCount++;
    }

    QString currentChatModel = "default";
    if (currentDb && currentDb->isOpen()) {
        if (currentLastNodeId != 0) {
            currentChatModel = currentDb->getChat(currentLastNodeId).model;
        } else {
            currentChatModel = m_newChatModel;
        }
    } else if (!currentDb || !currentDb->isOpen()) {
        chatModelMenu->setEnabled(false);
    }

    bool modelMatched = false;
    for (QAction* action : chatModelGroup->actions()) {
        if (action->data().toString() == currentChatModel) {
            action->setChecked(true);
            modelMatched = true;
            break;
        }
    }
    if (!modelMatched) mDef->setChecked(true);

    connect(chatModelGroup, &QActionGroup::triggered, this, [this](QAction* action) {
        if (currentDb && currentDb->isOpen()) {
            QString newModel = action->data().toString();
            if (currentLastNodeId == 0) {
                m_newChatModel = newModel;
            } else {
                ChatNode cnMod = currentDb->getChat(currentLastNodeId);
                cnMod.model = newModel;
                currentDb->updateChat(cnMod);
            }
            updateInputBehavior();
        }
    });

    QMenu* chatInputModeMenu = menu.addMenu(tr("Chat Input Mode"));
    QActionGroup* inputModeGroup = new QActionGroup(&menu);
    QAction* imDef = chatInputModeMenu->addAction(tr("Use Parent Default"));
    imDef->setData("default");
    imDef->setCheckable(true);
    QAction* imSingle = chatInputModeMenu->addAction(tr("Single Line"));
    imSingle->setData("Single Line");
    imSingle->setCheckable(true);
    QAction* imMulti = chatInputModeMenu->addAction(tr("Multi Line"));
    imMulti->setData("Multi Line");
    imMulti->setCheckable(true);

    inputModeGroup->addAction(imDef);
    inputModeGroup->addAction(imSingle);
    inputModeGroup->addAction(imMulti);

    QString currentInputMode = "default";
    if (currentDb && currentDb->isOpen()) {
        if (currentLastNodeId != 0) {
            currentInputMode = currentDb->getChat(currentLastNodeId).multiLine;
        } else {
            currentInputMode = m_newChatMultiLine;
        }
    } else if (!currentDb || !currentDb->isOpen()) {
        chatInputModeMenu->setEnabled(false);
    }

    if (currentInputMode == "Single Line")
        imSingle->setChecked(true);
    else if (currentInputMode == "Multi Line")
        imMulti->setChecked(true);
    else
        imDef->setChecked(true);

    connect(inputModeGroup, &QActionGroup::triggered, this, [this](QAction* action) {
        if (currentDb && currentDb->isOpen()) {
            QString newMode = action->data().toString();
            if (currentLastNodeId == 0) {
                m_newChatMultiLine = newMode;
            } else {
                ChatNode cnMulti = currentDb->getChat(currentLastNodeId);
                cnMulti.multiLine = newMode;
                currentDb->updateChat(cnMulti);
            }
            updateInputBehavior();
        }
    });

    QAction* editSystemPromptAction = menu.addAction(QIcon::fromTheme("configure"), tr("Chat Settings..."));
    connect(editSystemPromptAction, &QAction::triggered, this, [this]() { showChatSettingsDialog(currentLastNodeId); });

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

/** * @brief Spawns the modal settings dialogue to configure a specific chat node context. *  * This function is an
 * integral component of the MainWindow class structure. * It ensures that side effects map accurately to internal
 * application models. */
void MainWindow::showChatSettingsDialog(int messageId) {
    if (!currentDb || !currentDb->isOpen()) return;

    QString currentTitle;
    QString currentPrompt;
    QString currentBehavior = "default";
    QString currentModel = "default";
    QString currentMultiLine = "default";
    QString currentDraftPrompt;
    QString currentUserNote;
    QString endpointName = endpointComboBox ? endpointComboBox->currentText() : "Default";

    if (messageId == 0) {
        currentPrompt = m_newChatSystemPrompt;
        currentBehavior = m_newChatSendBehavior;
        currentModel = m_newChatModel;
        currentMultiLine = m_newChatMultiLine;
        currentDraftPrompt = m_newChatDraftPrompt;
        currentUserNote = m_newChatUserNote;
    } else {
        currentTitle = currentDb->getChat(messageId).title;
        currentPrompt = currentDb->getChat(messageId).systemPrompt;
        currentBehavior = currentDb->getChat(messageId).sendBehavior;
        currentModel = currentDb->getChat(messageId).model;
        currentMultiLine = currentDb->getChat(messageId).multiLine;
        currentDraftPrompt = currentDb->getChat(messageId).draftPrompt;
        currentUserNote = currentDb->getChat(messageId).userNote;
    }

    ChatSettingsDialog dlg(currentTitle, currentPrompt, currentBehavior, currentModel, currentMultiLine,
                           currentDraftPrompt, currentUserNote, endpointName, m_availableModels, this);
    if (dlg.exec() == QDialog::Accepted) {
        QString newTitle = dlg.getTitle();
        QString newPrompt = dlg.getSystemPrompt();
        QString newBehavior = dlg.getSendBehavior();
        QString newModel = dlg.getModel();
        QString newMultiLine = dlg.getMultiLine();
        QString newDraftPrompt = dlg.getDraftPrompt();
        QString newUserNote = dlg.getUserNote();

        if (messageId == 0) {
            m_newChatSystemPrompt = newPrompt;
            m_newChatSendBehavior = newBehavior;
            m_newChatModel = newModel;
            m_newChatMultiLine = newMultiLine;
            m_newChatDraftPrompt = newDraftPrompt;
            m_newChatUserNote = newUserNote;

            // Apply the draft right away if we are on the New Chat root screen
            if (!m_newChatDraftPrompt.isEmpty()) {
                if (!toggleInputModeBtn->isChecked()) {
                    inputField->setPlainText(m_newChatDraftPrompt);
                } else {
                    multiLineInput->setPlainText(m_newChatDraftPrompt);
                }
            }
        } else {
            ChatNode cn = currentDb->getChat(messageId);
            cn.title = newTitle;
            cn.systemPrompt = newPrompt;
            cn.sendBehavior = newBehavior;
            cn.model = newModel;
            cn.multiLine = newMultiLine;
            cn.draftPrompt = newDraftPrompt;
            cn.userNote = newUserNote;
            currentDb->updateChat(cn);
            loadDocumentsAndNotes();  // Refresh UI to potentially show new title
        }
        updateInputBehavior();
    }
}

/** * @brief Synchronizes the UI logic based on whether multi-line chat input is activated. *  * This function is an
 * integral component of the MainWindow class structure. * It ensures that side effects map accurately to internal
 * application models. */
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
            chatSetting = currentDb->getInheritedSetting(currentLastNodeId, "sendBehavior");
            if (chatSetting.isEmpty()) {
                chatSetting = "default";
            }
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

    if (currentDb && currentDb->isOpen()) {
        QString inheritedModel = "default";
        if (currentLastNodeId != 0) {
            inheritedModel = currentDb->getInheritedSetting(currentLastNodeId, "model");
        } else {
            inheritedModel = m_newChatModel;
        }

        if (!inheritedModel.isEmpty() && inheritedModel != "default") {
            m_selectedModel = inheritedModel;
            if (modelLabel) modelLabel->setText(tr("Model: %1 (Chat)").arg(m_selectedModel));
            if (modelSelectButton) modelSelectButton->setText(m_selectedModel);
        } else {
            QString dbModel = currentDb->getSetting("book", 0, "defaultModel", "");
            if (!dbModel.isEmpty()) {
                m_selectedModel = dbModel;
                if (modelLabel) modelLabel->setText(tr("Model: %1 (Book Default)").arg(m_selectedModel));
                if (modelSelectButton) modelSelectButton->setText(m_selectedModel);
            } else if (!m_availableModels.isEmpty()) {
                m_selectedModel = m_availableModels.first();
                if (modelLabel) modelLabel->setText(tr("Model: %1 (Global Fallback)").arg(m_selectedModel));
                if (modelSelectButton) modelSelectButton->setText(m_selectedModel);
            }
        }

        QString inheritedMultiLine = "default";
        if (currentLastNodeId != 0) {
            inheritedMultiLine = currentDb->getInheritedSetting(currentLastNodeId, "multiLine");
        } else {
            inheritedMultiLine = m_newChatMultiLine;
        }

        if (toggleInputModeBtn) {
            bool checked = toggleInputModeBtn->isChecked();
            if (inheritedMultiLine == "Multi Line" && !checked) {
                toggleInputModeBtn->blockSignals(true);
                toggleInputModeBtn->setChecked(true);
                chatStackWidget->setCurrentIndex(1);
                toggleInputModeBtn->blockSignals(false);
            } else if (inheritedMultiLine == "Single Line" && checked) {
                toggleInputModeBtn->blockSignals(true);
                toggleInputModeBtn->setChecked(false);
                chatStackWidget->setCurrentIndex(0);
                toggleInputModeBtn->blockSignals(false);
            }
        }
    }
}

/** * @brief Configures the window's title, geometry, and system tray icon based on prior settings. *  * This function
 * is an integral component of the MainWindow class structure. * It ensures that side effects map accurately to internal
 * application models. */
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

    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        trayIcon = new QSystemTrayIcon(QIcon(":/assets/icon.png"), this);
        QMenu* trayMenu = new QMenu(this);

        QAction* showAction = trayMenu->addAction(tr("Show KLlamaBooks"));
        connect(showAction, &QAction::triggered, this, &MainWindow::show);

        trayMenu->addSeparator();

        QAction* showQueueMenuAction = trayMenu->addAction(QIcon::fromTheme("view-list-details"), tr("Show Queue"));
        connect(showQueueMenuAction, &QAction::triggered, this, &MainWindow::showQueueWindow);

        QAction* showSpyMenuAction = trayMenu->addAction(QIcon::fromTheme("view-preview"), tr("Show Global Spy"));
        connect(showSpyMenuAction, &QAction::triggered, this, &MainWindow::showSpyWindow);

        trayMenu->addSeparator();

        QAction* quitAction = trayMenu->addAction(QIcon::fromTheme("application-exit"), tr("Quit"));
        connect(quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);

        trayIcon->setContextMenu(trayMenu);
        trayIcon->show();

        connect(trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
                if (isVisible()) {
                    hide();
                } else {
                    show();
                    raise();
                    activateWindow();
                }
            }
        });
    }
}

/** * @brief Populates the left-most list view with available `.db` files from the app data directory. *  * This
 * function is an integral component of the MainWindow class structure. * It ensures that side effects map accurately to
 * internal application models. */
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

/** * @brief Spawns a specialized modal dedicated to querying and managing installed Ollama models. *  * This function
 * is an integral component of the MainWindow class structure. * It ensures that side effects map accurately to internal
 * application models. */
void MainWindow::showModelExplorer() {
    ModelExplorer* explorer = new ModelExplorer(&ollamaClient, this);
    explorer->setAttribute(Qt::WA_DeleteOnClose);
    explorer->show();
}

/** * @brief Triggers the native file browser dialog to open an existing `.db` file. *  * This function is an integral
 * component of the MainWindow class structure. * It ensures that side effects map accurately to internal application
 * models. */
void MainWindow::onOpenBook() {
    QString startPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString filePath = QFileDialog::getOpenFileName(this, tr("Open Book"), startPath, tr("Books (*.db)"));
    if (!filePath.isEmpty()) {
        QFileInfo fileInfo(filePath);
        handleBookDrop(fileInfo.fileName());
    }
}

/** * @brief Action handler connecting to the explicit file close menu bar button. *  * This function is an integral
 * component of the MainWindow class structure. * It ensures that side effects map accurately to internal application
 * models. */
void MainWindow::onCloseBook() {
    if (currentDb && currentDb->isOpen()) {
        QFileInfo fileInfo(currentDb->filepath());
        closeBook(fileInfo.fileName());
    }
}

/** * @brief Locates the application's `.db` file directory using the desktop environment's file manager. *  * This
 * function is an integral component of the MainWindow class structure. * It ensures that side effects map accurately to
 * internal application models. */
void MainWindow::onOpenBookLocation() {
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

/** * @brief Spawns a dialog for the user to create a new SQLite Book file. *  * This function is an integral component
 * of the MainWindow class structure. * It ensures that side effects map accurately to internal application models. */
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

void MainWindow::showItemContextMenu(QStandardItem* item, const QPoint& globalPos) {
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

        QAction* selectedAction = menu.exec(globalPos);
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
                        m_selectedModel = dbModel;
                        modelLabel->setText(tr("Model: %1 (Book Default)").arg(m_selectedModel));
                    } else if (!m_availableModels.isEmpty()) {
                        m_selectedModel = m_availableModels.first();
                        modelLabel->setText(tr("Model: %1 (Global Fallback)").arg(m_selectedModel));
                    }
                    if (currentLastNodeId != 0 && mainContentStack->currentWidget() == chatWindowView) {
                        updateLinearChatView(currentLastNodeId, currentDb->getMessages());
                    }
                }
            }
        }
    } else if (type == "chats_folder" || type == "docs_folder" || type == "notes_folder" ||
               type == "templates_folder" || type == "drafts_folder" || (type == "chat_node" && item->rowCount() > 0)) {
        QMenu menu(this);
        QString actionName = "New Item";
        if (type == "chat_node") actionName = "New Chat";
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

        QAction* newAction = nullptr;
        QAction* newFromTemplateAction = nullptr;
        QAction* newFromPromptAction = nullptr;

        if (type == "docs_folder") {
            QMenu* newDocMenu = menu.addMenu(QIcon::fromTheme("document-new"), "New Document");
            newAction = newDocMenu->addAction("Blank Document");
            newFromTemplateAction = newDocMenu->addAction("From Template");
            newFromPromptAction = newDocMenu->addAction("From AI Prompt");
        } else {
            newAction = menu.addAction(actionName);
        }

        QAction* createFolderAction = nullptr;
        QAction* importFileAction = nullptr;
        if (type == "docs_folder" || type == "templates_folder" || type == "drafts_folder" || type == "notes_folder" ||
            type == "chats_folder") {
            createFolderAction = menu.addAction(QIcon::fromTheme("folder-new"), "Create Folder");
            if (type == "docs_folder" || type == "notes_folder" || type == "templates_folder" ||
                type == "drafts_folder") {
                importFileAction = menu.addAction(QIcon::fromTheme("document-import"), "Import from File...");
            }
        }

        QAction* importAction = nullptr;
        if (type == "chats_folder") {
            importAction = menu.addAction("Import Chat Session");
        }

        QAction* settingsAction = nullptr;
        if (type == "chat_node") {
            menu.addSeparator();
            settingsAction = menu.addAction(QIcon::fromTheme("configure"), "Chat Settings...");
        }

        QAction* selectedAction = menu.exec(globalPos);
        if (selectedAction == newAction) {
            addPhantomItem(item, type);
        } else if (settingsAction && selectedAction == settingsAction) {
            showChatSettingsDialog(item->data(Qt::UserRole).toInt());
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
        } else if (newAction && selectedAction == newAction) {
            QString name =
                QInputDialog::getText(this, "New Document", "Enter document name:", QLineEdit::Normal, "New Document");
            if (!name.isEmpty() && currentDb) {
                int folderId = item->data(Qt::UserRole).toInt();
                currentDb->addDocument(folderId, name, "");
                loadDocumentsAndNotes();
            }
        } else if (newFromTemplateAction && selectedAction == newFromTemplateAction) {
            // Placeholder: Not fully implemented yet
        } else if (newFromPromptAction && selectedAction == newFromPromptAction) {
            // Trigger AIOperationsDialog with preselected Fork mode
            AIOperationsDialog aiDlg(currentDb.get(), "", this);
            aiDlg.setForkOnlyMode(true);
            if (aiDlg.exec() == QDialog::Accepted) {
                QString prompt = aiDlg.getPrompt();
                if (!prompt.isEmpty() && currentDb) {
                    QString name = QString("Prompt: %1").arg(prompt.left(20));
                    int folderId = item->data(Qt::UserRole).toInt();
                    int newDocId = currentDb->addDocument(folderId, name, "");
                    // Enqueue the newly created prompt as a document generation request
                    // Using default values for model and parentId, and setting targetType="document" and
                    // targetAction="fork"
                    currentDb->enqueuePrompt(newDocId, "", prompt, 0, "document", 0, "fork");
                    loadDocumentsAndNotes();
                }
            }
        } else if (importFileAction && selectedAction == importFileAction) {
            QString fileName = QFileDialog::getOpenFileName(this, tr("Import File"), QDir::homePath(),
                                                            tr("Text Files (*.md *.txt);;All Files (*)"));
            if (!fileName.isEmpty() && currentDb) {
                QFile file(fileName);
                if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    QString content = file.readAll();
                    file.close();
                    QFileInfo fileInfo(fileName);
                    int parentId = item->data(Qt::UserRole).toInt();

                    if (type == "notes_folder") {
                        currentDb->addNote(parentId, fileInfo.fileName(), content);
                    } else if (type == "templates_folder") {
                        currentDb->addTemplate(parentId, fileInfo.fileName(), content);
                    } else if (type == "drafts_folder") {
                        currentDb->addDraft(parentId, fileInfo.fileName(), content);
                    } else {
                        currentDb->addDocument(parentId, fileInfo.fileName(), content);
                    }
                    loadDocumentsAndNotes();
                }
            }
        }
    } else if (type == "chat_session" || type == "chat_node") {
        QMenu menu(this);
        QAction* exportAction = nullptr;

        QAction* copyAction = menu.addAction(QIcon::fromTheme("edit-copy"), "Copy Message");
        QAction* pasteAction = menu.addAction(QIcon::fromTheme("edit-paste"), "Paste to Input");
        menu.addSeparator();
        QAction* forkAction = menu.addAction(QIcon::fromTheme("call-start"), "Fork from Here");
        menu.addSeparator();

        if (type == "chat_session") {
            exportAction = menu.addAction("Export Chat Session");
            menu.addSeparator();
        }

        QAction* settingsAction = menu.addAction(QIcon::fromTheme("configure"), "Chat Settings...");

        QAction* selectedAction = menu.exec(globalPos);
        if (selectedAction == copyAction) {
            QString fullText = item->text();
            int bracketIndex = fullText.indexOf("] ");
            if (bracketIndex != -1) fullText = fullText.mid(bracketIndex + 2);
            QGuiApplication::clipboard()->setText(fullText);
        } else if (selectedAction == pasteAction) {
            if (!toggleInputModeBtn->isChecked()) {
                inputField->setPlainText(inputField->toPlainText() + QGuiApplication::clipboard()->text());
            } else {
                multiLineInput->insertPlainText(QGuiApplication::clipboard()->text());
            }
        } else if (selectedAction == forkAction) {
            currentLastNodeId = item->data(Qt::UserRole).toInt();
            if (currentDb) {
                updateLinearChatView(currentLastNodeId, currentDb->getMessages());
                mainContentStack->setCurrentWidget(chatWindowView);
                chatTextArea->setFocus();
            }
        } else if (exportAction && selectedAction == exportAction) {
            exportChatSession();
        } else if (selectedAction == settingsAction) {
            showChatSettingsDialog(item->data(Qt::UserRole).toInt());
        }
    } else if (type == "document" || type == "note" || type == "template" || type == "draft") {
        QMenu menu(this);
        QAction* exportAction = nullptr;
        QAction* replaceAction = nullptr;
        QAction* loadTemplateAction = nullptr;
        QAction* resumeDraftAction = nullptr;

        if (type == "template") {
            loadTemplateAction = menu.addAction(QIcon::fromTheme("document-import"), "Load Template");
            menu.addSeparator();
        } else if (type == "draft") {
            resumeDraftAction = menu.addAction(QIcon::fromTheme("document-import"), "Resume Draft");
            menu.addSeparator();
        }

        QAction* discardAction = nullptr;
        if (type == "draft") {
            discardAction = menu.addAction(QIcon::fromTheme("edit-delete"), "Discard Draft");
            menu.addSeparator();
        }

        if (type == "document" || type == "note") {
            exportAction = menu.addAction(QIcon::fromTheme("document-export"), "Export Markdown...");
            replaceAction = menu.addAction(QIcon::fromTheme("document-import"), "Replace with Import...");
        }

        QAction* selectedAction = menu.exec(globalPos);
        if (loadTemplateAction && selectedAction == loadTemplateAction) {
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
        } else if (resumeDraftAction && selectedAction == resumeDraftAction) {
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
        } else if (discardAction && selectedAction == discardAction) {
            int docId = item->data(Qt::UserRole).toInt();
            if (currentDb) {
                currentDb->deleteDraft(docId);
                loadDocumentsAndNotes();
                statusBar->showMessage(tr("Draft discarded."), 3000);
            }
        } else if (exportAction && selectedAction == exportAction) {
            exportDocument(item->data(Qt::UserRole).toInt(), type);
        } else if (replaceAction && selectedAction == replaceAction) {
            QString fileName = QFileDialog::getOpenFileName(this, tr("Import File"), QDir::homePath(),
                                                            tr("Text Files (*.md *.txt);;All Files (*)"));
            if (!fileName.isEmpty() && currentDb) {
                QFile file(fileName);
                if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    QString content = file.readAll();
                    file.close();
                    QFileInfo fileInfo(fileName);
                    int id = item->data(Qt::UserRole).toInt();
                    if (type == "document") {
                        currentDb->updateDocument(id, fileInfo.fileName(), content);
                    } else if (type == "note") {
                        currentDb->updateNote(id, fileInfo.fileName(), content);
                    }
                    loadDocumentsAndNotes();
                }
            }
        }
    }
}

/** * @brief Draws the context menu for items listed in the right-pane virtual file explorer. *  * This function is an
 * integral component of the MainWindow class structure. * It ensures that side effects map accurately to internal
 * application models. */
void MainWindow::showVfsContextMenu(const QPoint& pos) {
    QModelIndex index = vfsExplorer->indexAt(pos);
    QStandardItem* treeItem = nullptr;

    if (index.isValid()) {
        QStandardItem* vfsItem = vfsModel->itemFromIndex(index);
        if (vfsItem && vfsItem->text() != "..") {
            // Find the corresponding item in openBooksModel
            QModelIndex parentIndex = openBooksTree->currentIndex();
            if (parentIndex.isValid() && openBooksModel) {
                QStandardItem* parentItem = openBooksModel->itemFromIndex(parentIndex);
                if (parentItem) {
                    for (int i = 0; i < parentItem->rowCount(); ++i) {
                        QStandardItem* child = parentItem->child(i);
                        if (child->text() == vfsItem->text() &&
                            child->data(Qt::UserRole) == vfsItem->data(Qt::UserRole) &&
                            child->data(Qt::UserRole + 1) == vfsItem->data(Qt::UserRole + 1)) {
                            treeItem = child;
                            break;
                        }
                    }
                }
            }
        } else if (vfsItem && vfsItem->text() == "..") {
            // Clicked '..' item, no context menu for now
            return;
        }
    }

    if (!treeItem) {
        // Empty space clicked, use the current folder in openBooksTree
        QModelIndex parentIndex = openBooksTree->currentIndex();
        if (parentIndex.isValid() && openBooksModel) {
            treeItem = openBooksModel->itemFromIndex(parentIndex);
        }
    }

    if (treeItem) {
        showItemContextMenu(treeItem, vfsExplorer->viewport()->mapToGlobal(pos));
    }
}

/** * @brief Handles loading a database file when double-clicked in the books list view. *  * This function is an
 * integral component of the MainWindow class structure. * It ensures that side effects map accurately to internal
 * application models. */
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
    QList<MessageNode> msgs = currentDb->getMessages();
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

/** * @brief Ijects a temporary visual placeholder item while a node is still being drafted to the database. *  * This
 * function is an integral component of the MainWindow class structure. * It ensures that side effects map accurately to
 * internal application models. */
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

/** * @brief Executes logic for loadSession. This function manages component initialization and handles state
 * transitions for the UI. *  * This function is an integral component of the MainWindow class structure. * It ensures
 * that side effects map accurately to internal application models. */
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

/** * @brief Executes logic for getPathToRoot. This function manages component initialization and handles state
 * transitions for the UI. *  * This function is an integral component of the MainWindow class structure. * It ensures
 * that side effects map accurately to internal application models. */
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
        // Force the linear path tracing to stop if we hit the currently selected node,
        // making it an explicit structural leaf/fork point in the UI trees.
        if (outChildren.size() == 1 && currentId != currentLastNodeId) {
            currentId = outChildren.first().id;
        } else {
            break;
        }
    }
    return currentId;
}

/** * @brief Executes logic for getChatNodeTitle. This function manages component initialization and handles state
 * transitions for the UI. *  * This function is an integral component of the MainWindow class structure. * It ensures
 * that side effects map accurately to internal application models. */
QString MainWindow::getChatNodeTitle(int nodeId, const QList<MessageNode>& allMessages) {
    if (!currentDb || !currentDb->isOpen()) return "New Chat";

    // Trace up the path to the root
    QList<MessageNode> path;
    getPathToRoot(nodeId, allMessages, path);

    // Look for a custom title in the settings database, starting from the leaf and going up
    for (int i = path.size() - 1; i >= 0; --i) {
        int currentId = path[i].id;
        QString customTitle = currentDb->getChat(currentId).title;
        if (!customTitle.isEmpty()) {
            return customTitle;
        }
    }

    // Default title fallback logic
    QString displayTitle;
    if (!path.isEmpty()) {
        displayTitle = path[0].content.simplified();
    }

    if (displayTitle.length() > 30) {
        displayTitle = displayTitle.left(27) + "...";
    }

    if (displayTitle.isEmpty()) {
        displayTitle = "New Chat";
    }

    return displayTitle;
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

            if (!db->getAllChatIds().contains(endNodeId)) {
                db->updateChat(db->getChat(endNodeId));  // This will initialize and save the node
            }

            QString displayTitle = getChatNodeTitle(endNodeId, allMessages);

            QStandardItem* item = nullptr;
            if (children.size() > 0) {
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

/** * @brief Helper to recursively map conversation database records to the `chatModel` hierarchy. *  * This function is
 * an integral component of the MainWindow class structure. * It ensures that side effects map accurately to internal
 * application models. */
void MainWindow::populateMessageForks(QStandardItem* parentItem, int parentId, const QList<MessageNode>& allMessages) {
    for (const auto& msg : allMessages) {
        if (msg.parentId == parentId) {
            // Find all children of this message
            QList<MessageNode> children;
            int endNodeId = getEndOfLinearPath(msg.id, allMessages, children);

            if (currentDb && !currentDb->getAllChatIds().contains(endNodeId)) {
                currentDb->updateChat(currentDb->getChat(endNodeId));
            }

            QString displayTitle = getChatNodeTitle(endNodeId, allMessages);

            QStandardItem* item = nullptr;
            if (children.size() > 0) {
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

/** * @brief Transforms the nonlinear chat forks into a single linear progression and renders it into the
 * `chatTextArea`. *  * This function is an integral component of the MainWindow class structure. * It ensures that side
 * effects map accurately to internal application models. */
void MainWindow::updateLinearChatView(int tailNodeId, const QList<MessageNode>& allMessages) {
    m_newChatSystemPrompt.clear();
    m_newChatSendBehavior = "default";
    m_newChatModel = "default";
    m_newChatMultiLine = "default";

    if (currentDb) {
        QString textToSave =
            !toggleInputModeBtn->isChecked() ? inputField->toPlainText() : multiLineInput->toPlainText();

        if (m_activeDraftNodeId != 0) {
            ChatNode cnDraft = currentDb->getChat(m_activeDraftNodeId);
            if (cnDraft.version == m_activeDraftVersion) {
                cnDraft.draftPrompt = textToSave;
                currentDb->updateChat(cnDraft);
            }
        } else {
            m_newChatDraftPrompt = textToSave;
        }
    }

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
                        foundFolderItem = findItemById(folderItem, tailNodeId);
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
        if (node.id == -1) continue;  // Do not render the phantom user node in the chat view itself

        QString roleName = (node.role == "user") ? userName : assistantName;

        QTextCursor cursor(chatTextArea->document());
        cursor.movePosition(QTextCursor::End);
        chatTextArea->setTextCursor(cursor);

        chatTextArea->insertHtml(QString("<div style='font-weight: bold;'>[%1]</div>").arg(roleName.toHtmlEscaped()));
        chatTextArea->insertPlainText("\n" + node.content + "\n\n");
    }

    chatTextArea->blockSignals(false);
    if (discardChangesBtn) discardChangesBtn->hide();

    QString savedDraft = "";
    m_activeDraftNodeId = tailNodeId;
    if (currentDb) {
        if (tailNodeId != 0) {
            ChatNode activeNode = currentDb->getChat(tailNodeId);
            savedDraft = activeNode.draftPrompt;
            m_activeDraftVersion = activeNode.version;
        } else {
            savedDraft = m_newChatDraftPrompt;
            m_activeDraftVersion = 0;
        }
    }

    if (!toggleInputModeBtn->isChecked()) {
        inputField->setPlainText(savedDraft);
    } else {
        multiLineInput->setPlainText(savedDraft);
    }

    if (savedDraft.isEmpty()) {
        dismissDraftBtn->hide();
    } else {
        dismissDraftBtn->show();
    }

    if (tailNodeId != 0) {
        m_newChatDraftPrompt.clear();
        m_newChatUserNote.clear();
    }

    updateBreadcrumbs();
}

/** * @brief Updates the header label to display the current conversation path string. *  * This function is an integral
 * component of the MainWindow class structure. * It ensures that side effects map accurately to internal application
 * models. */
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

    QList<QStandardItem*> pathItems;
    QStandardItem* curr = treeItem;
    while (curr) {
        pathItems.prepend(curr);
        curr = curr->parent();
    }

    for (int i = 0; i < pathItems.size(); ++i) {
        QStandardItem* nodeItem = pathItems[i];
        if (i < pathItems.size() - 1) {
            QPushButton* btn = new QPushButton(nodeItem->text(), this);
            btn->setFlat(true);
            btn->setCursor(Qt::PointingHandCursor);
            connect(btn, &QPushButton::clicked, this,
                    [this, nodeItem]() { openBooksTree->setCurrentIndex(nodeItem->index()); });
            breadcrumbLayout->addWidget(btn);
            breadcrumbLayout->addWidget(new QLabel(">", this));
        } else {
            QLineEdit* edit = new QLineEdit(nodeItem->text(), this);
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

/** * @brief Executes logic for onBreadcrumbClicked. This function manages component initialization and handles state
 * transitions for the UI. *  * This function is an integral component of the MainWindow class structure. * It ensures
 * that side effects map accurately to internal application models. */
void MainWindow::onBreadcrumbClicked(const QString& type, int id) {
    // Allows jumping up the tree
}

/** * @brief Executes logic for onRenameCurrentItem. This function manages component initialization and handles state
 * transitions for the UI. *  * This function is an integral component of the MainWindow class structure. * It ensures
 * that side effects map accurately to internal application models. */
void MainWindow::onRenameCurrentItem() {
    // Enables inline rename
}

/** * @brief Triggers a reload of the currently open document or note from the persistent database. *  * This function
 * is an integral component of the MainWindow class structure. * It ensures that side effects map accurately to internal
 * application models. */
void MainWindow::onDiscardChanges() {
    if (!currentDb || currentLastNodeId == 0) return;
    updateLinearChatView(currentLastNodeId, currentDb->getMessages());
    discardChangesBtn->hide();
    statusBar->showMessage(tr("Changes discarded."), 3000);
}

/** * @brief Discards the current draft, clearing text fields and updating the database. *  * This function
 * is an integral component of the MainWindow class structure. * It ensures that side effects map accurately to internal
 * application models. */
void MainWindow::onDismissDraft() {
    m_newChatDraftPrompt.clear();

    inputField->blockSignals(true);
    multiLineInput->blockSignals(true);
    inputField->setPlainText("");
    multiLineInput->setPlainText("");
    inputField->blockSignals(false);
    multiLineInput->blockSignals(false);

    if (m_activeDraftNodeId != 0 && currentDb) {
        ChatNode cnDraft = currentDb->getChat(m_activeDraftNodeId);
        if (cnDraft.version == m_activeDraftVersion) {
            cnDraft.draftPrompt = "";
            if (currentDb->updateChat(cnDraft)) {
                m_activeDraftVersion++; // Sync with optimistic lock
            }
        }
    }

    dismissDraftBtn->hide();
    statusBar->showMessage(tr("Draft dismissed."), 3000);
}

/** * @brief Executes logic for populateTree. This function manages component initialization and handles state
 * transitions for the UI. *  * This function is an integral component of the MainWindow class structure. * It ensures
 * that side effects map accurately to internal application models. */
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

/**
 * @brief Core dispatch mechanism for ingesting user input and initiating the LLM generation cycle.
 *
 * Executed when the user hits the Enter key or Send button.
 * 1. Resolves whether to pull text from the single-line input field or the multi-line editor box.
 * 2. Enforces validation (e.g., ensuring a model is selected).
 * 3. Commits the raw user string to the SQLite database as a new 'user' role message node.
 *    - If starting a new session (currentLastNodeId == 0), it generates a short title and
 *      places it in the correct chats folder.
 *    - If continuing an existing session, it appends it as a child of the current leaf node.
 * 4. Pushes the newly created interaction state to the QueueManager to begin background network processing.
 * 5. Safely cleans the input buffers and synchronizes the UI views to reflect the new state.
 */
void MainWindow::onSendMessage() {
    if (!currentDb) return;

    QString text;
    if (!toggleInputModeBtn->isChecked()) {
        text = inputField->toPlainText().trimmed();
        if (text.isEmpty()) return;
    } else {
        text = multiLineInput->toPlainText();
        if (text.isEmpty()) return;
    }

    bool hasActiveGeneration = false;
    QueueItem item;

    // Check if there are any pending or processing tasks for the current chat path
    QList<QueueItem> queue = currentDb->getQueue();
    for (const auto& qItem : queue) {
        if ((qItem.state == "pending" || qItem.state == "processing") && qItem.targetType == "message") {
            bool isInCurrentPath = false;
            if (qItem.messageId == currentLastNodeId) {
                isInCurrentPath = true;
            } else {
                for (const auto& msg : currentChatPath) {
                    if (msg.id == qItem.messageId) {
                        isInCurrentPath = true;
                        break;
                    }
                }
            }

            if (isInCurrentPath) {
                hasActiveGeneration = true;
                item = qItem;
                break; // Found conflicting generation
            }
        }
    }

    if (hasActiveGeneration) {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle(tr("Active Generation Conflict"));
        msgBox.setText(tr(
            "This chat is currently generating a response.\nWhat would you like to do with your new message?"));

        QPushButton* queueBtn = msgBox.addButton(tr("Queue to send later"), QMessageBox::AcceptRole);
        QPushButton* forkBtn = msgBox.addButton(tr("Fork and send"), QMessageBox::AcceptRole);
        QPushButton* cancelReplaceBtn =
            msgBox.addButton(tr("Cancel previous and replace"), QMessageBox::AcceptRole);
        QPushButton* draftsBtn = msgBox.addButton(tr("Save to drafts"), QMessageBox::ActionRole);
        QPushButton* ignoreBtn = msgBox.addButton(tr("Ignore text and clear"), QMessageBox::DestructiveRole);
        QPushButton* cancelBtn = msgBox.addButton(tr("Cancel"), QMessageBox::RejectRole);

        msgBox.exec();

        QAbstractButton* clicked = msgBox.clickedButton();
        if (clicked == draftsBtn) {
            currentDb->addDraft(0, text.left(30) + "...", text);
            if (!toggleInputModeBtn->isChecked())
                inputField->clear();
            else
                multiLineInput->clear();
            loadDocumentsAndNotes();
            return;
        } else if (clicked == ignoreBtn) {
            if (!toggleInputModeBtn->isChecked())
                inputField->clear();
            else
                multiLineInput->clear();
            return;
        } else if (clicked == cancelBtn) {
            return;  // Abort sending, keep text
        } else if (clicked == cancelReplaceBtn) {
            QueueManager::instance().cancelItem(currentDb, item.id);
        } else if (clicked == forkBtn) {
            // To fork from the parent of the currently generating node:
            int forkParentId = 0;
            if (currentChatPath.size() >= 2) {
                forkParentId = currentChatPath[currentChatPath.size() - 2].id;
            }

            if (!toggleInputModeBtn->isChecked())
                inputField->clear();
            else
                multiLineInput->clear();

            int userMsgId = currentDb->addMessage(forkParentId, text, "user");
            int aiId = currentDb->addMessage(userMsgId, "", "assistant");
            currentLastNodeId = aiId;

            QString model = m_selectedModel;
            if (model.isEmpty() && !m_availableModels.isEmpty()) {
                model = m_availableModels.first();
            }

            QueueManager::instance().enqueuePrompt(aiId, model, text);

            loadDocumentsAndNotes();  // Refresh tree
            updateLinearChatView(currentLastNodeId, currentDb->getMessages());
            statusBar->showMessage(tr("Fork task queued."), 3000);
            return;
        } else if (clicked == queueBtn) {
            // Fallthrough to normal send, it will queue naturally because it is an enqueuePrompt call.
        }
    }

    if (!toggleInputModeBtn->isChecked()) {
        inputField->clear();
    } else {
        multiLineInput->clear();
    }

    bool forkCreated = false;
    // Passive edit check
    if (!currentChatPath.isEmpty() && currentLastNodeId != 0 && !isCreatingNewChat) {
        QTextDocument* doc = chatTextArea->document();
        QString currentRole = "";
        QString currentContent = "";
        int msgIndex = 0;

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

    bool wasCreatingNewChat = isCreatingNewChat;

    int parentId = wasCreatingNewChat ? 0 : currentLastNodeId;
    int folderId = wasCreatingNewChat ? currentChatFolderId : 0;
    isCreatingNewChat = false;

    // 1. Add User message
    int userMsgId = currentDb->addMessage(parentId, text, "user", folderId);

    if (parentId == 0) {
        if (!m_newChatSystemPrompt.isEmpty()) {
            ChatNode cnSys = currentDb->getChat(userMsgId);
            cnSys.systemPrompt = m_newChatSystemPrompt;
            currentDb->updateChat(cnSys);
            m_newChatSystemPrompt.clear();
        }
        if (m_newChatSendBehavior != "default") {
            ChatNode cnBeh = currentDb->getChat(userMsgId);
            cnBeh.sendBehavior = m_newChatSendBehavior;
            currentDb->updateChat(cnBeh);
            m_newChatSendBehavior = "default";
        }
        if (m_newChatModel != "default") {
            ChatNode cnMod = currentDb->getChat(userMsgId);
            cnMod.model = m_newChatModel;
            currentDb->updateChat(cnMod);
            m_newChatModel = "default";
        }
        if (m_newChatMultiLine != "default") {
            ChatNode cnMulti = currentDb->getChat(userMsgId);
            cnMulti.multiLine = m_newChatMultiLine;
            currentDb->updateChat(cnMulti);
            m_newChatMultiLine = "default";
        }
    }

    // 2. Add Assistant placeholder
    int aiId = currentDb->addMessage(userMsgId, "", "assistant");
    currentLastNodeId = aiId;

    // 3. Enqueue
    QueueManager::instance().enqueuePrompt(aiId, m_selectedModel, text);

    // 4. Refresh tree if needed (especially for new chats)
    if (forkCreated) {
        loadDocumentsAndNotes();  // Ensure tree refreshes correctly to natively reflect any new forks
    } else if (wasCreatingNewChat) {
        QModelIndex idx = openBooksTree->currentIndex();
        QStandardItem* item = openBooksModel->itemFromIndex(idx);
        if (item && item->data(Qt::UserRole + 1).toString() == "chat_session") {
            item->setData(aiId, Qt::UserRole);
            item->setData("chat_node", Qt::UserRole + 1);
            item->setText(text.left(30));
            QFont f = item->font();
            f.setItalic(false);
            item->setFont(f);
        }
    } else {
        QStandardItem* item = findItemInTree(parentId, "chat_node");
        if (item) {
            if (item->rowCount() > 0) {
                QStandardItem* branchItem = new QStandardItem(QIcon::fromTheme("text-x-generic"), text.left(30));
                branchItem->setData(aiId, Qt::UserRole);
                branchItem->setData("chat_node", Qt::UserRole + 1);
                item->appendRow(branchItem);
            } else {
                item->setData(aiId, Qt::UserRole);
                item->setText(text.left(30));
            }
        }
    }

    // 5. Update view
    if (currentDb) {
        ChatNode cnDraft = currentDb->getChat(currentLastNodeId);
        // Note: the active draft logic only applies if currentLastNodeId is the active draft node
        if (currentLastNodeId == m_activeDraftNodeId && cnDraft.version == m_activeDraftVersion) {
            cnDraft.draftPrompt = "";
            currentDb->updateChat(cnDraft);
        } else if (currentLastNodeId != 0 && m_activeDraftNodeId == 0) {
            // New chat - just clear it safely
            cnDraft.draftPrompt = "";
            currentDb->updateChat(cnDraft);
        }
    }
    updateLinearChatView(currentLastNodeId, currentDb->getMessages());
    statusBar->showMessage(tr("Request queued."), 3000);
}

/** * @brief Executes logic for onOllamaChunk. This function manages component initialization and handles state
 * transitions for the UI. *  * This function is an integral component of the MainWindow class structure. * It ensures
 * that side effects map accurately to internal application models. */
void MainWindow::onOllamaChunk(const QString& chunk) {}
/** * @brief Executes logic for onOllamaComplete. This function manages component initialization and handles state
 * transitions for the UI. *  * This function is an integral component of the MainWindow class structure. * It ensures
 * that side effects map accurately to internal application models. */
void MainWindow::onOllamaComplete(const QString& fullResponse) {}
/** * @brief Executes logic for onOllamaError. This function manages component initialization and handles state
 * transitions for the UI. *  * This function is an integral component of the MainWindow class structure. * It ensures
 * that side effects map accurately to internal application models. */
void MainWindow::onOllamaError(const QString& error) {}

void MainWindow::onQueueChunk(std::shared_ptr<BookDatabase> db, int messageId, const QString& chunk,
                              const QString& targetType) {
    if (currentDb == db) {
        if (targetType == "document" && currentDocumentId == messageId) {
            documentEditorView->blockSignals(true);
            QTextCursor cursor = documentEditorView->textCursor();
            cursor.movePosition(QTextCursor::End);
            documentEditorView->setTextCursor(cursor);
            documentEditorView->insertPlainText(chunk);
            documentEditorView->blockSignals(false);
        } else if (targetType == "message" && currentLastNodeId == messageId) {
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
}

/** * @brief Clears the Global Spy UI whenever a new task is ingested. *  * This function is an integral component of
 * the MainWindow class structure. * It ensures that side effects map accurately to internal application models. */
void MainWindow::onProcessingStarted(std::shared_ptr<BookDatabase> db, int messageId, const QString& targetType) {
    if (currentDb == db) {
        if (targetType == "document" && currentDocumentId == messageId) {
            // Keep document read-only and let the generation happen silently in the background
            // The result will be available for review in DocumentReviewDialog once completed.
            statusBar->showMessage(tr("AI is generating document changes..."));
        } else if (targetType == "message" && currentLastNodeId == messageId) {
            statusBar->showMessage(tr("LLM is responding..."));
        }
    }
    updateQueueStatus();
}

void MainWindow::onProcessingFinished(std::shared_ptr<BookDatabase> db, int messageId, bool success,
                                      const QString& targetType) {
    if (currentDb == db) {
        if (targetType == "document" && currentDocumentId == messageId) {
            statusBar->showMessage(success ? tr("AI document generation complete. Pending review.")
                                           : tr("Error generating document changes."),
                                   3000);
        } else if (targetType == "message" && currentLastNodeId == messageId) {
            statusBar->showMessage(success ? tr("Response complete.") : tr("Error in response."), 3000);
            updateLinearChatView(currentLastNodeId, currentDb->getMessages());
        }
    }
    updateQueueStatus();
}

/** * @brief Receives download percentage increments from the Ollama model registry client. *  * This function is an
 * integral component of the MainWindow class structure. * It ensures that side effects map accurately to internal
 * application models. */
void MainWindow::onPullProgressUpdated(const QString& modelName, int percent, const QString& status) {
    statusLabel->setText(QString("Downloading %1: %2% (%3)").arg(modelName).arg(percent).arg(status));
}

/** * @brief Fired when a model completes downloading, tearing down progress UI elements. *  * This function is an
 * integral component of the MainWindow class structure. * It ensures that side effects map accurately to internal
 * application models. */
void MainWindow::onPullFinished(const QString& modelName) {
    statusLabel->setText(QString("Download complete: %1").arg(modelName));
    QTimer::singleShot(3000, this, [this]() {
        if (statusLabel->text().startsWith("Download complete:")) {
            statusLabel->setText(tr("Ready"));
        }
    });
}

/** * @brief Callback that listens for inline text edits on standard tree items and flushes the title back to the DB. *
 * * This function is an integral component of the MainWindow class structure. * It ensures that side effects map
 * accurately to internal application models. */
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

/** * @brief Fires when a distinct node is focused in the chat navigation tree, refreshing the right-pane. *  * This
 * function is an integral component of the MainWindow class structure. * It ensures that side effects map accurately to
 * internal application models. */
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

/**
 * @brief Global event filter for the main window, intercepting drags, drops, and mouse events.
 *
 * @param obj The object that received the event.
 * @param event The event being filtered.
 * @return true if the event was handled and consumed, false otherwise.
 */
bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
    if (obj == connectionStatusLabel && event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            ollamaClient.fetchModels();
            return true;
        }
    }

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

        QWidget* sourceWidget = qobject_cast<QWidget*>(dragEvent->source());
        QAbstractItemView* sourceView = qobject_cast<QAbstractItemView*>(sourceWidget);
        if (!sourceView && sourceWidget && sourceWidget->parentWidget()) {
            sourceView = qobject_cast<QAbstractItemView*>(sourceWidget->parentWidget());
        }

        if (dragEvent->mimeData()->hasUrls()) {
            dragEvent->acceptProposedAction();
            return true;
        }
        if ((obj == openBooksTree || obj == openBooksTree->viewport()) && dragEvent->source() == bookList) {
            dragEvent->acceptProposedAction();
            return true;
        }
        if ((obj == bookList || obj == bookList->viewport()) && sourceView == openBooksTree) {
            dragEvent->acceptProposedAction();
            return true;
        }
        if (sourceView == openBooksTree || sourceView == vfsExplorer) {
            return false;  // let native handling show drop indicator
        }
    } else if (event->type() == QEvent::DragMove) {
        QDragMoveEvent* dragEvent = static_cast<QDragMoveEvent*>(event);
        if (dragEvent->mimeData()->hasUrls()) {
            dragEvent->acceptProposedAction();
            return true;
        }
        if (obj == openBooksTree || obj == openBooksTree->viewport()) {
            return false;  // let native handling show drop indicator
        } else if (obj == vfsExplorer || obj == vfsExplorer->viewport()) {
            return false;  // let native handling show drop indicator
        }
    } else if (event->type() == QEvent::Drop) {
        QDropEvent* dropEvent = static_cast<QDropEvent*>(event);

        QWidget* sourceWidget = qobject_cast<QWidget*>(dropEvent->source());
        QAbstractItemView* sourceView = qobject_cast<QAbstractItemView*>(sourceWidget);
        if (!sourceView && sourceWidget && sourceWidget->parentWidget()) {
            sourceView = qobject_cast<QAbstractItemView*>(sourceWidget->parentWidget());
        }

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
                } else if ((fileInfo.suffix() == "md" || fileInfo.suffix() == "txt") && currentDb) {
                    QFile file(filePath);
                    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                        QString content = file.readAll();
                        file.close();

                        QAbstractItemView* targetView = (obj == openBooksTree || obj == openBooksTree->viewport())
                                                            ? static_cast<QAbstractItemView*>(openBooksTree)
                                                            : static_cast<QAbstractItemView*>(vfsExplorer);

                        QModelIndex targetIndex = targetView->indexAt(dropEvent->position().toPoint());
                        QStandardItem* targetItem = nullptr;

                        if (targetIndex.isValid()) {
                            if (targetView == openBooksTree) {
                                targetItem = openBooksModel->itemFromIndex(targetIndex);
                            } else {
                                QStandardItem* vfsItem = vfsModel->itemFromIndex(targetIndex);
                                if (vfsItem && vfsItem->text() == "..") {
                                    QModelIndex treeIndex = openBooksTree->currentIndex();
                                    if (treeIndex.isValid()) {
                                        QStandardItem* currentFolder = openBooksModel->itemFromIndex(treeIndex);
                                        if (currentFolder && currentFolder->parent())
                                            targetItem = currentFolder->parent();
                                    }
                                } else if (vfsItem) {
                                    int id = vfsItem->data(Qt::UserRole).toInt();
                                    QString type = vfsItem->data(Qt::UserRole + 1).toString();
                                    QModelIndex treeIndex = openBooksTree->currentIndex();
                                    if (treeIndex.isValid()) {
                                        QStandardItem* book = openBooksModel->itemFromIndex(treeIndex);
                                        while (book && book->parent()) book = book->parent();
                                        targetItem = findItemRecursive(book, id, type);
                                    }
                                }
                            }
                        } else if (targetView == vfsExplorer) {
                            QModelIndex treeIndex = openBooksTree->currentIndex();
                            if (treeIndex.isValid()) targetItem = openBooksModel->itemFromIndex(treeIndex);
                        }

                        int targetFolderId = 0;
                        QString targetType = "documents";

                        if (targetItem) {
                            targetType = targetItem->data(Qt::UserRole + 1).toString();
                            targetFolderId = targetItem->data(Qt::UserRole).toInt();
                            if (!targetType.endsWith("_folder")) {
                                if (targetItem->parent()) {
                                    targetType = targetItem->parent()->data(Qt::UserRole + 1).toString();
                                    targetFolderId = targetItem->parent()->data(Qt::UserRole).toInt();
                                }
                            }
                        }

                        if (targetType == "notes_folder") {
                            currentDb->addNote(targetFolderId, fileInfo.fileName(), content);
                        } else if (targetType == "templates_folder") {
                            currentDb->addTemplate(targetFolderId, fileInfo.fileName(), content);
                        } else if (targetType == "drafts_folder") {
                            currentDb->addDraft(targetFolderId, fileInfo.fileName(), content);
                        } else {
                            currentDb->addDocument(targetFolderId, fileInfo.fileName(), content);
                        }

                        loadDocumentsAndNotes();
                        dropEvent->acceptProposedAction();
                        return true;
                    }
                }
            }
        } else if ((obj == openBooksTree || obj == openBooksTree->viewport() || obj == vfsExplorer ||
                    obj == vfsExplorer->viewport()) &&
                   (sourceView == openBooksTree || sourceView == vfsExplorer)) {
            QAbstractItemView* targetView = (obj == openBooksTree || obj == openBooksTree->viewport())
                                                ? static_cast<QAbstractItemView*>(openBooksTree)
                                                : static_cast<QAbstractItemView*>(vfsExplorer);

            QModelIndex targetIndex = targetView->indexAt(dropEvent->position().toPoint());
            QStandardItem* targetItem = nullptr;

            if (targetIndex.isValid()) {
                if (targetView == openBooksTree) {
                    targetItem = openBooksModel->itemFromIndex(targetIndex);
                } else {
                    QStandardItem* vfsItem = vfsModel->itemFromIndex(targetIndex);
                    if (vfsItem && vfsItem->text() == "..") {
                        QModelIndex treeIndex = openBooksTree->currentIndex();
                        if (treeIndex.isValid()) {
                            QStandardItem* currentFolder = openBooksModel->itemFromIndex(treeIndex);
                            if (currentFolder && currentFolder->parent()) targetItem = currentFolder->parent();
                        }
                    } else if (vfsItem) {
                        int id = vfsItem->data(Qt::UserRole).toInt();
                        QString type = vfsItem->data(Qt::UserRole + 1).toString();
                        QModelIndex treeIndex = openBooksTree->currentIndex();
                        if (treeIndex.isValid()) {
                            QStandardItem* book = openBooksModel->itemFromIndex(treeIndex);
                            while (book && book->parent()) book = book->parent();
                            targetItem = findItemRecursive(book, id, type);
                        }
                    }
                }
            } else if (targetView == vfsExplorer) {
                // Background drop in VFS explorer - move to current viewing folder
                QModelIndex treeIndex = openBooksTree->currentIndex();
                if (treeIndex.isValid()) targetItem = openBooksModel->itemFromIndex(treeIndex);
            }

            if (targetItem) {
                QModelIndex sourceIndex = sourceView->currentIndex();
                if (!sourceIndex.isValid() && sourceView->selectionModel()->hasSelection()) {
                    sourceIndex = sourceView->selectionModel()->selectedIndexes().first();
                }

                if (sourceIndex.isValid()) {
                    QStandardItem* draggedItem = nullptr;
                    if (sourceView == openBooksTree) {
                        draggedItem = openBooksModel->itemFromIndex(sourceIndex);
                    } else if (sourceView == vfsExplorer) {
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
                    } else {
                        dropEvent->ignore();
                    }
                    return true;
                }
            }
            dropEvent->ignore();
            return true;
        } else if ((obj == openBooksTree || obj == openBooksTree->viewport()) && dropEvent->source() == bookList) {
            QListWidgetItem* item = bookList->currentItem();
            if (item) {
                QString fileName = item->text();
                handleBookDrop(fileName);
                dropEvent->acceptProposedAction();
                return true;
            }
        } else if ((obj == bookList || obj == bookList->viewport()) && sourceView == openBooksTree) {
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
    /** * @brief Executes logic for eventFilter. This function manages component initialization and handles state
     * transitions for the UI. *  * This function is an integral component of the MainWindow class structure. * It
     * ensures that side effects map accurately to internal application models. */
    return KXmlGuiWindow::eventFilter(obj, event);
}

/** * @brief Copies an external database file dropped into the application to the app data location and loads it. *  *
 * This function is an integral component of the MainWindow class structure. * It ensures that side effects map
 * accurately to internal application models. */
void MainWindow::handleBookDrop(const QString& fileName) {
    // Find the item in bookList and click it programmatically or just replicate the open logic
    for (int i = 0; i < bookList->count(); ++i) {
        if (bookList->item(i)->text() == fileName) {
            onBookSelected(bookList->model()->index(i, 0));
            return;
        }
    }
}

/** * @brief Closes an active SQLite database and flushes it from the view. *  * This function is an integral component
 * of the MainWindow class structure. * It ensures that side effects map accurately to internal application models. */
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

/** * @brief Constructs the right-click menu for the raw database files view. *  * This function is an integral
 * component of the MainWindow class structure. * It ensures that side effects map accurately to internal application
 * models. */
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

/** * @brief Force-refreshes the virtual file explorer list. *  * This function is an integral component of the
 * MainWindow class structure. * It ensures that side effects map accurately to internal application models. */
void MainWindow::refreshVfsExplorer() {
    QModelIndex index = openBooksTree->currentIndex();
    if (!index.isValid()) return;
    QStandardItem* item = openBooksModel->itemFromIndex(index);
    if (!item) return;

    QString type = item->data(Qt::UserRole + 1).toString();
    vfsModel->clear();

    if (type != "book") {
        QStandardItem* upItem = new QStandardItem(QIcon::fromTheme("go-up"), "..");
        vfsModel->appendRow(upItem);
    }

    for (int i = 0; i < item->rowCount(); ++i) {
        QStandardItem* childTreeItem = item->child(i);
        QStandardItem* childItem = new QStandardItem(childTreeItem->icon(), childTreeItem->text());
        childItem->setData(childTreeItem->data(Qt::UserRole), Qt::UserRole);
        childItem->setData(childTreeItem->data(Qt::UserRole + 1), Qt::UserRole + 1);
        vfsModel->appendRow(childItem);
    }
}

/** * @brief Callback for item selection in the `openBooksTree`, which dynamically loads the `vfsExplorer` contents. *
 * * This function is an integral component of the MainWindow class structure. * It ensures that side effects map
 * accurately to internal application models. */
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
        refreshVfsExplorer();

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

namespace {
void saveExpandedState(QTreeView* tree, QStandardItem* item, QSet<QString>& expanded) {
    if (!item) return;
    if (tree->isExpanded(item->index())) {
        QString path = item->text();
        QStandardItem* p = item->parent();
        while (p) {
            path = p->text() + "/" + path;
            p = p->parent();
        }
        expanded.insert(path);
    }
    for (int i = 0; i < item->rowCount(); ++i) saveExpandedState(tree, item->child(i), expanded);
}
void restoreExpandedState(QTreeView* tree, QStandardItem* item, const QSet<QString>& expanded) {
    if (!item) return;
    QString path = item->text();
    QStandardItem* p = item->parent();
    while (p) {
        path = p->text() + "/" + path;
        p = p->parent();
    }
    if (expanded.contains(path)) tree->setExpanded(item->index(), true);
    for (int i = 0; i < item->rowCount(); ++i) restoreExpandedState(tree, item->child(i), expanded);
}
}  // namespace

/** * @brief Reloads the primary side-pane `openBooksTree` showing all folders, documents, and chat records. *  * This
 * function is an integral component of the MainWindow class structure. * It ensures that side effects map accurately to
 * internal application models. */
void MainWindow::loadDocumentsAndNotes() {
    if (!openBooksModel) return;

    QModelIndex currentIndex = openBooksTree->currentIndex();
    int currId = 0;
    QString currType;
    if (currentIndex.isValid()) {
        QStandardItem* currItem = openBooksModel->itemFromIndex(currentIndex);
        if (currItem) {
            currId = currItem->data(Qt::UserRole).toInt();
            currType = currItem->data(Qt::UserRole + 1).toString();
        }
    }

    QSet<QString> expanded;
    for (int i = 0; i < openBooksModel->rowCount(); ++i) {
        saveExpandedState(openBooksTree, openBooksModel->item(i), expanded);
    }

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
            populateChatFolders(chatsFolder, 0, currentDb->getMessages(), db.get());
        }
    }

    for (int i = 0; i < openBooksModel->rowCount(); ++i) {
        restoreExpandedState(openBooksTree, openBooksModel->item(i), expanded);
    }

    if (currId != 0 || !currType.isEmpty()) {
        QStandardItem* newItem = nullptr;

        newItem = findItemInTree(currId, currType);

        if (newItem) {
            openBooksTree->selectionModel()->blockSignals(true);
            openBooksTree->setCurrentIndex(newItem->index());
            openBooksTree->selectionModel()->select(newItem->index(),
                                                    QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Current);
            openBooksTree->selectionModel()->blockSignals(false);

            // If the item itself changed content in the database, we need to refresh the view pane
            // without needing the user to explicitly un-select and re-select it.
            // But we don't want to re-trigger the entire selection logic to avoid UI jumping.
            // Let's manually trigger the content refresh if it's a document/note being viewed.
            if (currType == "document" || currType == "template" || currType == "draft") {
                QString outTitle, outContent;
                getDocumentContent(currId, currType, outTitle, outContent);
                documentEditorView->blockSignals(true);
                documentEditorView->setPlainText(outContent);
                documentEditorView->blockSignals(false);
                // Also update the title in the tree view if it changed.
                newItem->setText(outTitle);

                // Ensure the docContainer is correctly visible after tree reload hides it
                mainContentStack->setCurrentWidget(docContainer);
            } else if (currType == "note") {
                QString outTitle, outContent;
                getDocumentContent(currId, currType, outTitle, outContent);
                noteEditorView->blockSignals(true);
                noteEditorView->setPlainText(outContent);
                noteEditorView->blockSignals(false);
                newItem->setText(outTitle);

                // Ensure the noteContainer is correctly visible
                mainContentStack->setCurrentWidget(noteContainer);
            }
        }
    }

    // Refresh VFS in case folder contents changed
    refreshVfsExplorer();
}

// Replaced showDocumentsContextMenu and showNotesContextMenu with showVfsContextMenu

/**
 * @brief Constructs and displays the context menu for managing items inside the main tree view.
 *
 * @param pos The coordinate position to spawn the menu.
 */
void MainWindow::showOpenBookContextMenu(const QPoint& pos) {
    QModelIndex index = openBooksTree->indexAt(pos);
    if (!index.isValid()) return;

    QStandardItem* item = openBooksModel->itemFromIndex(index);
    if (!item) return;

    showItemContextMenu(item, openBooksTree->viewport()->mapToGlobal(pos));
}

/** * @brief Executes logic for getDocumentContent. This function manages component initialization and handles state
 * transitions for the UI. *  * This function is an integral component of the MainWindow class structure. * It ensures
 * that side effects map accurately to internal application models. */
void MainWindow::getDocumentContent(int id, const QString& type, QString& outTitle, QString& outContent) {
    if (!currentDb) return;
    if (type == "document") {
        QList<DocumentNode> docs = currentDb->getDocuments();
        for (const auto& doc : docs) {
            if (doc.id == id) {
                outContent = doc.content;
                outTitle = doc.title;
                return;
            }
        }
    } else if (type == "note") {
        QList<NoteNode> notes = currentDb->getNotes();
        for (const auto& note : notes) {
            if (note.id == id) {
                outContent = note.content;
                outTitle = note.title;
                return;
            }
        }
    }
}

/** * @brief Exports a selected document item into a standalone file on disk. *  * This function is an integral
 * component of the MainWindow class structure. * It ensures that side effects map accurately to internal application
 * models. */
void MainWindow::exportDocument(int id, const QString& type) {
    if (!currentDb) return;

    QString content;
    QString defaultTitle;

    getDocumentContent(id, type, defaultTitle, content);

    if (content.isEmpty() && defaultTitle.isEmpty()) return;

    if (defaultTitle.isEmpty()) {
        defaultTitle = "export";
    }
    if (!defaultTitle.endsWith(".md")) {
        defaultTitle += ".md";
    }

    QString defaultPath = QDir::homePath() + "/" + defaultTitle;
    QString fileName = QFileDialog::getSaveFileName(this, tr("Export Markdown"), defaultPath,
                                                    tr("Markdown Files (*.md);;Text Files (*.txt);;All Files (*)"));
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << content;
        file.close();
        statusBar->showMessage(tr("Exported to %1").arg(fileName), 3000);
    } else {
        QMessageBox::warning(this, tr("Export Error"), tr("Could not save to file: %1").arg(file.errorString()));
    }
}

/** * @brief Serializes a database conversation tree into an external file format. *  * This function is an integral
 * component of the MainWindow class structure. * It ensures that side effects map accurately to internal application
 * models. */
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

/** * @brief Reads a JSON or standardized file containing conversation metadata into the active book. *  * This function
 * is an integral component of the MainWindow class structure. * It ensures that side effects map accurately to internal
 * application models. */
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

/**
 * @brief Searches the top-level open books model for an item matching the database node criteria.
 *
 * @param id The database row ID of the item.
 * @param type The generic schema type of the item.
 * @return The matching QStandardItem, or nullptr if not found.
 */
/**
 * @brief Recursively searches for an item by its integer ID.
 *
 * @param parent The root of the sub-tree to search.
 * @param id The database row ID to find.
 * @return The matching QStandardItem, or nullptr if not found.
 */
QStandardItem* MainWindow::findItemById(QStandardItem* parent, int id) {
    if (parent->data(Qt::UserRole).toInt() == id) return parent;
    for (int i = 0; i < parent->rowCount(); ++i) {
        QStandardItem* found = findItemById(parent->child(i), id);
        if (found) return found;
    }
    return nullptr;
}

/** * @brief Executes logic for findItemInTree. This function manages component initialization and handles state
 * transitions for the UI. *  * This function is an integral component of the MainWindow class structure. * It ensures
 * that side effects map accurately to internal application models. */
QStandardItem* MainWindow::findItemInTree(int id, const QString& type) {
    if (!openBooksModel || openBooksModel->rowCount() == 0) return nullptr;
    return findItemRecursive(openBooksModel->invisibleRootItem(), id, type);
}

/**
 * @brief Recursively traverses the standard item model tree looking for a specific node.
 *
 * @param parent The root of the sub-tree to search.
 * @param id The database row ID.
 * @param type The database schema type.
 * @return The matching QStandardItem, or nullptr if not found.
 */
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

/** * @brief Updates the UI counter and styles depending on whether the background worker queue has tasks. *  * This
 * function is an integral component of the MainWindow class structure. * It ensures that side effects map accurately to
 * internal application models. */
void MainWindow::updateQueueStatus() {
    int total = QueueManager::instance().totalPendingCount();
    queueStatusBtn->setText(QString("Q: %1").arg(total));
    if (QueueManager::instance().isProcessing()) {
        queueStatusBtn->setStyleSheet("background-color: #2196F3; color: white;");
    } else {
        queueStatusBtn->setStyleSheet("");
    }
}

/** * @brief Fills the notification tray menu indicating task progress states. *  * This function is an integral
 * component of the MainWindow class structure. * It ensures that side effects map accurately to internal application
 * models. */
void MainWindow::updateNotificationStatus() {
    int total = 0;
    notificationMenu->clear();
    for (auto db : QueueManager::instance().databases()) {
        if (!db || !db->isOpen()) continue;
        auto notifications = db->getNotifications();
        total += notifications.size();
        for (const auto& n : notifications) {
            QString bookName = QFileInfo(db->filepath()).fileName();
            QString typeStr =
                (n.type == "error") ? tr("Error") : (n.type == "review_needed" ? tr("Review Needed") : tr("Finished"));
            QAction* action = notificationMenu->addAction(
                QString("[%1] %2: Msg %3").arg(bookName, typeStr, QString::number(n.messageId)));
            connect(action, &QAction::triggered, this, [this, db, n, bookName]() {
                if (n.type == "review_needed") {
                    DocumentReviewDialog reviewDlg(db, n.messageId, this);
                    if (reviewDlg.exec() == QDialog::Accepted) {
                        db->dismissNotification(n.id);
                        updateNotificationStatus();
                    }

                    if (currentDb == db && currentDocumentId != 0) {
                        // Re-fetch document content in case the review dialog changed it or we need to clear live
                        // preview.
                        QString outTitle, outContent;
                        getDocumentContent(currentDocumentId, "document", outTitle, outContent);
                        documentEditorView->blockSignals(true);
                        documentEditorView->setPlainText(outContent);
                        documentEditorView->blockSignals(false);
                    }
                } else {
                    QDialog dialog(this);
                    dialog.setWindowTitle(tr("Notification Summary"));
                    QVBoxLayout* layout = new QVBoxLayout(&dialog);

                    QString typeStr = (n.type == "error") ? tr("Error") : tr("Finished");
                    QLabel* summary =
                        new QLabel(QString("<b>Book:</b> %1<br><b>Status:</b> %2<br><b>Message ID:</b> %3")
                                       .arg(bookName, typeStr, QString::number(n.messageId)));
                    summary->setWordWrap(true);
                    layout->addWidget(summary);

                    QHBoxLayout* btnLayout = new QHBoxLayout();
                    QPushButton* gotoBtn = new QPushButton(tr("Goto Item"), &dialog);
                    QPushButton* dismissBtn = new QPushButton(tr("Dismiss"), &dialog);
                    QPushButton* closeBtn = new QPushButton(tr("Close"), &dialog);

                    btnLayout->addWidget(gotoBtn);
                    btnLayout->addWidget(dismissBtn);
                    btnLayout->addWidget(closeBtn);
                    layout->addLayout(btnLayout);

                    connect(gotoBtn, &QPushButton::clicked, [&]() {
                        onQueueItemClicked(db, n.messageId);
                        dialog.accept();
                    });

                    connect(dismissBtn, &QPushButton::clicked, [&]() {
                        db->dismissNotification(n.id);
                        updateNotificationStatus();
                        dialog.accept();
                    });

                    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

                    dialog.exec();
                }
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

/** * @brief Executes logic for showNotificationMenu. This function manages component initialization and handles state
 * transitions for the UI. *  * This function is an integral component of the MainWindow class structure. * It ensures
 * that side effects map accurately to internal application models. */
void MainWindow::showNotificationMenu() { notificationBtn->showMenu(); }

/** * @brief Raises and focuses the global tracking window indicating processing logs. *  * This function is an integral
 * component of the MainWindow class structure. * It ensures that side effects map accurately to internal application
 * models. */
void MainWindow::showQueueWindow() {
    if (m_queueWindow) {
        m_queueWindow->show();
        m_queueWindow->raise();
        m_queueWindow->activateWindow();
        return;
    }
    QueueWindow* qw = new QueueWindow(nullptr);
    qw->setAttribute(Qt::WA_DeleteOnClose);
    qw->show();
    m_queueWindow = qw;
}

class GlobalSpyWindow : public QWidget {
   public:
    GlobalSpyWindow(QWidget* parent = nullptr) : QWidget(parent, Qt::Window) {
        setWindowTitle(tr("Global Spying"));
        resize(800, 600);
        setAttribute(Qt::WA_DeleteOnClose);

        QVBoxLayout* baseLayout = new QVBoxLayout(this);

        m_spyTextEdit = new QTextEdit(this);
        m_spyTextEdit->setReadOnly(true);
        m_spyTextEdit->document()->setMaximumBlockCount(1000); // Act as a circular buffer
        baseLayout->addWidget(m_spyTextEdit);

        if (OllamaClient* client = QueueManager::instance().client()) {
            connect(client, &OllamaClient::requestSent, this,
                    [this](const QString& model, const QString& systemPrompt, const QString& prompt) {
                        m_spyTextEdit->moveCursor(QTextCursor::End);
                        m_spyTextEdit->insertHtml(QString("<hr><b>[Request Sent]</b> Model: %1<br>").arg(model.toHtmlEscaped()));
                        if (!systemPrompt.isEmpty()) {
                            m_spyTextEdit->insertHtml(QString("<b>System:</b><br>%1<br>").arg(systemPrompt.toHtmlEscaped().replace("\n", "<br>")));
                        }
                        m_spyTextEdit->insertHtml(QString("<b>User:</b><br>%1<br><br><b>Response:</b><br>").arg(prompt.toHtmlEscaped().replace("\n", "<br>")));
                        m_spyTextEdit->moveCursor(QTextCursor::End);
                    });
        }

        connect(&QueueManager::instance(), &QueueManager::processingChunk, this,
                [this](std::shared_ptr<BookDatabase> db, int messageId, const QString& chunk, const QString& type) {
                    m_spyTextEdit->moveCursor(QTextCursor::End);
                    m_spyTextEdit->insertPlainText(chunk);
                    m_spyTextEdit->moveCursor(QTextCursor::End);
                });

        connect(&QueueManager::instance(), &QueueManager::processingFinished, this,
                [this](std::shared_ptr<BookDatabase> db, int messageId, bool success, const QString& type) {
                    m_spyTextEdit->moveCursor(QTextCursor::End);
                    m_spyTextEdit->insertHtml(success ? "<br><br><b>[Finished]</b><br>" : "<br><br><b><font color='red'>[Error]</font></b><br>");
                    m_spyTextEdit->moveCursor(QTextCursor::End);
                });
    }

   private:
    QTextEdit* m_spyTextEdit;
};

void MainWindow::showSpyWindow() {
    if (m_spyWindow) {
        m_spyWindow->show();
        m_spyWindow->raise();
        m_spyWindow->activateWindow();
        return;
    }

    m_spyWindow = new GlobalSpyWindow(nullptr);
    m_spyWindow->show();
}

/** * @brief Executes logic for onQueueItemClicked. This function manages component initialization and handles state
 * transitions for the UI. *  * This function is an integral component of the MainWindow class structure. * It ensures
 * that side effects map accurately to internal application models. */
void MainWindow::onQueueItemClicked(std::shared_ptr<BookDatabase> db, int messageId) {
    if (!db) return;

    // Check if this is a document queue item that is completed.
    // If we double-click a queue item, the "messageId" parameter might actually be the queue ID
    // or the target document ID. Let's find the queue item to be safe.
    // Wait, the signal sends messageId. Let's look up if there's a queue item with this messageId that is completed.
    bool openedReview = false;
    for (const auto& item : db->getQueue()) {
        if (item.messageId == messageId && item.targetType == "document" && item.state == "completed") {
            DocumentReviewDialog reviewDlg(db, item.id, this);
            if (reviewDlg.exec() == QDialog::Accepted) {
                db->dismissNotificationByMessageId(item.id);
                updateNotificationStatus();
            }
            openedReview = true;

            // Reload document editor view just in case the dialog made edits (replace/append)
            // or to revert a live preview if the user discarded.
            if (currentDocumentId == messageId) {
                QString outTitle, outContent;
                getDocumentContent(currentDocumentId, "document", outTitle, outContent);
                documentEditorView->blockSignals(true);
                documentEditorView->setPlainText(outContent);
                documentEditorView->blockSignals(false);
            }
            break;
        }
    }

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

    QStringList types = {"chat_node", "document", "note", "template", "draft"};
    QStandardItem* foundItem = nullptr;
    for (const QString& type : types) {
        foundItem = findItemInTree(messageId, type);
        if (foundItem) break;
    }

    if (foundItem) {
        openBooksTree->setCurrentIndex(foundItem->index());
        openBooksTree->selectionModel()->select(foundItem->index(),
                                                QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Current);
        openBooksTree->scrollTo(foundItem->index());
    } else if (!openedReview) {
        // Fallback to chat node logic just in case it wasn't rendered yet
        currentLastNodeId = messageId;
        updateLinearChatView(currentLastNodeId, currentDb->getMessages());
        mainContentStack->setCurrentWidget(chatWindowView);
    }

    db->dismissNotificationByMessageId(messageId);
    updateNotificationStatus();
}

/** * @brief Adds custom visual badges (like error or pending states) over items in the VFS explorer. *  * This function
 * is an integral component of the MainWindow class structure. * It ensures that side effects map accurately to internal
 * application models. */
void MainWindow::updateVfsMarkers(const QList<Notification>& notifications) {
    if (vfsModel) {
        for (int i = 0; i < vfsModel->rowCount(); ++i) {
            QStandardItem* item = vfsModel->item(i);
            int id = item->data(Qt::UserRole).toInt();
            QString itemType = item->data(Qt::UserRole + 1).toString();
            int nType = 0;
            if (itemType == "chat_node") {
                for (const auto& n : notifications) {
                    if (n.messageId == id && !n.isDismissed) {
                        nType = (n.type == "error") ? 2 : 1;
                        break;
                    }
                }
            }
            item->setData(nType, Qt::UserRole + 10);
        }
    }
    if (forkExplorerModel) {
        for (int i = 0; i < forkExplorerModel->rowCount(); ++i) {
            QStandardItem* item = forkExplorerModel->item(i);
            int id = item->data(Qt::UserRole).toInt();
            QString itemType = item->data(Qt::UserRole + 1).toString();
            int nType = 0;
            if (itemType == "chat_node") {
                for (const auto& n : notifications) {
                    if (n.messageId == id && !n.isDismissed) {
                        nType = (n.type == "error") ? 2 : 1;
                        break;
                    }
                }
            }
            item->setData(nType, Qt::UserRole + 10);
        }
    }
}

/** * @brief Executes logic for updateTreeMarkersRecursive. This function manages component initialization and handles
 * state transitions for the UI. *  * This function is an integral component of the MainWindow class structure. * It
 * ensures that side effects map accurately to internal application models. */
void MainWindow::updateTreeMarkersRecursive(QStandardItem* parent, const QList<Notification>& notifications) {
    if (!parent) return;
    for (int i = 0; i < parent->rowCount(); ++i) {
        QStandardItem* child = parent->child(i);
        if (!child) continue;
        int messageId = child->data(Qt::UserRole).toInt();
        QString itemType = child->data(Qt::UserRole + 1).toString();
        int notifyType = 0;
        if (itemType == "chat_node") {
            for (const auto& n : notifications) {
                if (n.messageId == messageId && !n.isDismissed) {
                    notifyType = (n.type == "error") ? 2 : 1;
                    break;
                }
            }
        }
        child->setData(notifyType, Qt::UserRole + 10);
        updateTreeMarkersRecursive(child, notifications);
    }
}

/**
 * @brief Executes a drag-and-drop move or copy operation between database folders.
 *
 * @param draggedItem The standard item being moved.
 * @param targetItem The destination folder item.
 * @param isCopy True if this is a duplication operation, false for a standard move.
 * @return true if the operation succeeded, false otherwise.
 */
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
        } else if ((itemType == "chat_session" || itemType == "chat_node") && targetType == "chats_folder") {
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
    int dbItemId = itemId;
    if ((itemType == "chat_session" || itemType == "chat_node") && targetType == "chats_folder") {
        if (itemType == "chat_node") {
            if (draggedItem->parent() && draggedItem->parent()->data(Qt::UserRole + 1).toString() == "chats_folder") {
                QList<MessageNode> path;
                getPathToRoot(itemId, currentDb->getMessages(), path);
                if (!path.isEmpty()) {
                    dbItemId = path.first().id;
                } else {
                    return false;
                }
            } else {
                return false;
            }
        }
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
            QTimer::singleShot(0, this, [this]() { loadDocumentsAndNotes(); });
            return true;
        }
    }

    if (compatible && db->moveItem(table, dbItemId, targetFolderId)) {
        QTimer::singleShot(0, this, [this]() { loadDocumentsAndNotes(); });
        return true;
    }
    return false;
}

/** * @brief Executes logic for showDocumentAIToolsMenu. This function manages component initialization and handles
 * state transitions for the UI. *  * This function is an integral component of the MainWindow class structure. * It
 * ensures that side effects map accurately to internal application models. */
void MainWindow::showDocumentAIToolsMenu() {}

void MainWindow::onDocumentAIOperations() {
    if (m_isGenerating || !currentDb) return;

    AIOperationsDialog dialog(currentDb.get(), "", this);
    if (dialog.exec() == QDialog::Accepted) {
        QString op = dialog.getOperation();
        QString promptTpl = dialog.getPrompt();
        QString contextText;

        if (op == "complete") {
            QTextCursor cursor = documentEditorView->textCursor();
            contextText = documentEditorView->toPlainText().left(cursor.position());
            if (contextText.isEmpty()) {
                contextText = documentEditorView->toPlainText();
            }
        } else if (op == "replace") {
            contextText = documentEditorView->toPlainText();
        } else if (op == "replace_in_place") {
            QTextCursor cursor = documentEditorView->textCursor();
            if (!cursor.hasSelection()) {
                QMessageBox::information(this, tr("Replace in place"), tr("Please select some text first."));
                return;
            }
            contextText = cursor.selectedText();
            contextText.replace(QChar::ParagraphSeparator, '\n');
        }

        QString prompt = promptTpl;
        prompt.replace("{context}", contextText);

        QString model = m_selectedModel;
        if (model.isEmpty() && !m_availableModels.isEmpty()) {
            model = m_availableModels.first();
        }

        QueueManager::instance().enqueuePrompt(currentDocumentId, model, prompt, 0, "document", 0, "");
        statusBar->showMessage(tr("AI document task queued."), 3000);
    }
}

void MainWindow::onDocumentHistory() {
    if (!currentDb || currentDocumentId == 0) return;
    DocumentHistoryDialog dialog(currentDb, currentDocumentId, this);
    if (dialog.exec() == QDialog::Accepted) {
        // Refresh the document view to show restored content
        QString outTitle, outContent;
        getDocumentContent(currentDocumentId, "document", outTitle, outContent);
        documentEditorView->blockSignals(true);
        documentEditorView->setPlainText(outContent);
        documentEditorView->blockSignals(false);
        statusBar->showMessage(tr("Document history restored."), 3000);
    }
}

/**
 * @brief Handles double-click events on the main tree view to open documents or chats.
 *
 * @param index The model index of the double-clicked item.
 */
void MainWindow::onOpenBooksTreeDoubleClicked(const QModelIndex& index) {
    QStandardItem* item = openBooksModel->itemFromIndex(index);
    if (!item || !currentDb) return;

    QString type = item->data(Qt::UserRole + 1).toString();
    if (type == "document" || type == "template" || type == "draft") {
        int docId = item->data(Qt::UserRole).toInt();

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
        return;
    }

    if (item->data(Qt::UserRole).toInt() > 0 && type != "doc_folder" && type != "templates_folder" &&
        type != "drafts_folder") {
        // It's a chat node. Single clicking displays the chat.
        currentLastNodeId = item->data(Qt::UserRole).toInt();
        updateLinearChatView(currentLastNodeId, currentDb->getMessages());
        mainContentStack->setCurrentWidget(chatWindowView);
    }
}

/**
 * @brief Handles double-click events in the virtual file system explorer view.
 *
 * @param index The model index of the double-clicked item.
 */
void MainWindow::onVfsExplorerDoubleClicked(const QModelIndex& index) {
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

    QStandardItem* found = findItemInTree(nodeId, type);
    if (found) openBooksTree->setCurrentIndex(found->index());
}

/**
 * @brief Persists the active document editor state back to the SQLite database.
 *
 * Activated by the 'Save Document' button or equivalent shortcuts. It evaluates whether the
 * document is a newly minted unsaved buffer (isCreatingNewDoc) or an existing file being updated.
 * It resolves the target parent folder ID and utilizes the respective BookDatabase method
 * (addDocument, addTemplate, updateDocument, etc.) based on the item type metadata.
 * If the document was being auto-saved as a transient draft, the temporary draft entry is purged
 * upon successful explicit save.
 */
void MainWindow::onEditDocument() {
    if (!currentDb || currentDocumentId == 0) return;

    if (m_openDocEditors.contains(currentDocumentId)) {
        DocumentEditWindow* win = m_openDocEditors.value(currentDocumentId);
        if (win) {
            win->show();
            win->raise();
            win->activateWindow();
            return;
        } else {
            m_openDocEditors.remove(currentDocumentId);
        }
    }

    QModelIndex index = openBooksTree->currentIndex();
    QStandardItem* item = index.isValid() ? openBooksModel->itemFromIndex(index) : nullptr;
    QString currentTitle = item ? item->text() : "Document";

    DocumentEditWindow* editWin = new DocumentEditWindow(currentDb, currentDocumentId, currentTitle);
    m_openDocEditors.insert(currentDocumentId, editWin);

    connect(editWin, &DocumentEditWindow::documentModified, this, [this](int docId) {
        loadDocumentsAndNotes();  // Refresh tree
        if (currentDocumentId == docId) {
            QString outTitle, outContent;
            getDocumentContent(docId, "document", outTitle, outContent);
            documentEditorView->blockSignals(true);
            documentEditorView->setPlainText(outContent);
            documentEditorView->blockSignals(false);
        }
    });

    connect(editWin, &DocumentEditWindow::newDocumentCreated, this, [this](int) {
        loadDocumentsAndNotes();  // Refresh tree
    });

    connect(editWin, &QObject::destroyed, this, [this, id = currentDocumentId]() { m_openDocEditors.remove(id); });
    editWin->show();
}

void MainWindow::onSaveDocBtnClicked() {
    // Unused, but kept for compatibility or future note saving logic if merged
}

/**
 * @brief Persists the active note editor state back to the SQLite database.
 *
 * Parallel in structure to onSaveDocBtnClicked, but exclusively targets the 'notes' schema.
 * Determines if a new note record needs to be created or if an existing note (currentNoteId)
 * should be updated. It identically cleans up any lingering auto-drafts associated with the session.
 */
void MainWindow::onSaveNoteBtnClicked() {
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
        if (currentAutoDraftId != 0) {
            currentDb->deleteDraft(currentAutoDraftId);
            currentAutoDraftId = 0;
        }
        item->setData(newId, Qt::UserRole);
        item->setData("note", Qt::UserRole + 1);
        item->setText(currentTitle);
        QFont f = item->font();
        f.setItalic(false);
        item->setFont(f);
        isCreatingNewNote = false;
        return;
    }
    if (currentNoteId == 0) return;
    currentDb->updateNote(currentNoteId, currentTitle, noteEditorView->toPlainText());
    if (currentAutoDraftId != 0) {
        currentDb->deleteDraft(currentAutoDraftId);
        currentAutoDraftId = 0;
    }
    statusBar->showMessage(tr("Note saved."), 3000);
    if (item) item->setText(currentTitle);
}

/**
 * @brief Shows a custom context menu for selected text in the chat window to quickly create nodes.
 *
 * @param pos The position where the context menu was requested.
 */
void MainWindow::onChatTextAreaContextMenu(const QPoint& pos) {
    QMenu* menu = chatTextArea->createStandardContextMenu();

    // Find which message was clicked based on cursor position
    QTextCursor cursor = chatTextArea->cursorForPosition(pos);
    QTextBlock block = cursor.block();

    QString expectedUserBlock =
        QString("[%1]").arg(currentDb ? currentDb->getSetting("book", 0, "userName", "User") : "User");
    QString expectedAssistantBlock =
        QString("[%1]").arg(currentDb ? currentDb->getSetting("book", 0, "assistantName", "Assistant") : "Assistant");

    int msgIndex = -1;
    QTextDocument* doc = chatTextArea->document();
    int currentIndex = -1;

    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        QString bText = b.text().trimmed();
        if (bText == expectedUserBlock || bText == expectedAssistantBlock) {
            currentIndex++;
        }
        if (b == block) {
            msgIndex = currentIndex;
        }
    }

    QAction* forkAction = nullptr;
    QAction* copyAction = nullptr;
    QAction* infoAction = nullptr;
    QAction* reuseAction = nullptr;

    if (msgIndex >= 0 && msgIndex < currentChatPath.size()) {
        menu->addSeparator();
        forkAction = menu->addAction(QIcon::fromTheme("call-start"), "Reply to this and fork here");
        copyAction = menu->addAction(QIcon::fromTheme("edit-copy"), "Copy message");
        reuseAction = menu->addAction(QIcon::fromTheme("edit-redo"), "Reuse this");

        bool isAssistant = (currentChatPath[msgIndex].role == "assistant");
        infoAction =
            menu->addAction(QIcon::fromTheme("dialog-information"), isAssistant ? "Response info" : "Message info");
    }

    menu->addSeparator();
    QAction* saveDraftAction = menu->addAction(QIcon::fromTheme("document-save-as"), "Save chat as draft");
    QAction* saveTemplateAction = menu->addAction(QIcon::fromTheme("document-save-as"), "Save chat as template");

    QAction* selectedAction = menu->exec(chatTextArea->viewport()->mapToGlobal(pos));
    if (selectedAction == saveDraftAction) {
        bool ok;
        QString name = QInputDialog::getText(this, "Save as Draft", "Draft Name:", QLineEdit::Normal, "New Draft", &ok);
        if (ok && !name.isEmpty() && currentDb) {
            currentDb->addDraft(0, name, chatTextArea->toPlainText());
            loadDocumentsAndNotes();
        }
    } else if (selectedAction == saveTemplateAction) {
        bool ok;
        QString name =
            QInputDialog::getText(this, "Save as Template", "Template Name:", QLineEdit::Normal, "New Template", &ok);
        if (ok && !name.isEmpty() && currentDb) {
            currentDb->addTemplate(0, name, chatTextArea->toPlainText());
            loadDocumentsAndNotes();
        }
    } else if (selectedAction == forkAction && forkAction) {
        currentLastNodeId = currentChatPath[msgIndex].id;
        if (currentDb) {
            loadDocumentsAndNotes();
            updateLinearChatView(currentLastNodeId, currentDb->getMessages());
            mainContentStack->setCurrentWidget(chatWindowView);
        }
        if (!toggleInputModeBtn->isChecked()) {
            inputField->setFocus();
        } else {
            multiLineInput->setFocus();
        }
    } else if (selectedAction == copyAction && copyAction) {
        QApplication::clipboard()->setText(currentChatPath[msgIndex].content);
        statusBar->showMessage(tr("Message copied to clipboard."), 3000);
    } else if (selectedAction == reuseAction && reuseAction) {
        QString textToReuse;
        if (currentChatPath[msgIndex].role == "user") {
            textToReuse = currentChatPath[msgIndex].content;
        } else {
            for (int i = msgIndex - 1; i >= 0; --i) {
                if (currentChatPath[i].role == "user") {
                    textToReuse = currentChatPath[i].content;
                    break;
                }
            }
            if (textToReuse.isEmpty()) {
                textToReuse = currentChatPath[msgIndex].content;  // fallback
            }
        }
        if (!toggleInputModeBtn->isChecked() && !textToReuse.contains('\n')) {
            inputField->setPlainText(textToReuse);
            inputField->setFocus();
        } else {
            if (!toggleInputModeBtn->isChecked()) toggleInputModeBtn->setChecked(true);
            multiLineInput->setPlainText(textToReuse);
            multiLineInput->setFocus();
        }
    } else if (selectedAction == infoAction && infoAction) {
        QDialog infoDialog(this);
        bool isAssistant = (currentChatPath[msgIndex].role == "assistant");
        infoDialog.setWindowTitle(isAssistant ? "Response info" : "Message info");
        QVBoxLayout* infoLayout = new QVBoxLayout(&infoDialog);

        QString infoText =
            QString("<b>Date/Time:</b> %1").arg(currentChatPath[msgIndex].timestamp.toString("yyyy-MM-dd HH:mm:ss"));

        if (isAssistant && currentDb) {
            QString model = currentDb->getSetting("message", currentChatPath[msgIndex].id, "model", "Unknown");
            QString sysPrompt =
                currentDb->getSetting("message", currentChatPath[msgIndex].id, "systemPrompt", "Unknown");
            infoText += QString("<br><b>Model:</b> %1<br><b>System Prompt:</b> %2")
                            .arg(model.toHtmlEscaped(), sysPrompt.toHtmlEscaped().replace("\n", "<br>"));
        }

        QLabel* infoLabel = new QLabel(infoText, &infoDialog);
        infoLabel->setWordWrap(true);
        infoLabel->setTextFormat(Qt::RichText);
        infoLayout->addWidget(infoLabel);

        QLabel* notesLabel = new QLabel("Comments:", &infoDialog);
        infoLayout->addWidget(notesLabel);

        QTextEdit* notesEdit = new QTextEdit(&infoDialog);
        int commentId = -1;
        if (currentDb) {
            QList<CommentNode> existing = currentDb->getComments("message", currentChatPath[msgIndex].id);
            if (!existing.isEmpty()) {
                commentId = existing.first().id;
                notesEdit->setPlainText(existing.first().content);
            }
        }
        infoLayout->addWidget(notesEdit);

        QDialogButtonBox* buttonBox =
            new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, Qt::Horizontal, &infoDialog);
        connect(buttonBox, &QDialogButtonBox::accepted, &infoDialog, &QDialog::accept);
        connect(buttonBox, &QDialogButtonBox::rejected, &infoDialog, &QDialog::reject);
        infoLayout->addWidget(buttonBox);

        if (infoDialog.exec() == QDialog::Accepted && currentDb) {
            QString newText = notesEdit->toPlainText();
            if (commentId == -1 && !newText.isEmpty()) {
                currentDb->addComment("message", currentChatPath[msgIndex].id, newText);
            } else if (commentId != -1) {
                if (newText.isEmpty()) {
                    currentDb->deleteComment(commentId);
                } else {
                    currentDb->updateComment(commentId, newText);
                }
            }
        }
    }
    delete menu;
}

/**
 * @brief Handles double-click events in the chat fork explorer to switch to a specific branch.
 *
 * @param index The model index of the double-clicked item.
 */
void MainWindow::onChatForkExplorerDoubleClicked(const QModelIndex& index) {
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
}

/**
 * @brief Shows a context menu in the chat fork explorer for options like branching or prompting.
 *
 * @param pos The position where the context menu was requested.
 */
void MainWindow::onChatForkExplorerContextMenu(const QPoint& pos) {
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
}
