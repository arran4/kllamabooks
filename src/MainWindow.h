#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <KXmlGuiWindow>
#include <QAction>
#include <QComboBox>
#include <QDateTime>
#include <QFutureWatcher>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPointer>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QStringList>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTextBrowser>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <memory>

#include "BookDatabase.h"
#include "ChatInputWidget.h"
#include "ModelExplorer.h"
#include "OllamaClient.h"
#include "SettingsDialog.h"

class CustomItemModel : public QStandardItemModel {
    Q_OBJECT
   public:
    explicit CustomItemModel(QObject* parent = nullptr);
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    void setMainWindow(class MainWindow* mainWindow) { m_mainWindow = mainWindow; }

   private:
    class MainWindow* m_mainWindow;
};

class MainWindow : public KXmlGuiWindow {
    Q_OBJECT
   public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();
    void getDocumentContent(int id, const QString& type, QString& outTitle, QString& outContent);

   private slots:

    void onOpenBooksTreeDoubleClicked(const QModelIndex& index);
    void onVfsExplorerDoubleClicked(const QModelIndex& index);
    void onSaveDocBtnClicked();
    void onSaveNoteBtnClicked();
    void onChatTextAreaContextMenu(const QPoint& pos);
    void onChatForkExplorerDoubleClicked(const QModelIndex& index);
    void onChatForkExplorerContextMenu(const QPoint& pos);

    void onCreateBook();
    void onOpenBook();
    void onCloseBook();
    void onOpenBookLocation();
    void showModelExplorer();
    void showSettingsDialog();
    void onBookSelected(const QModelIndex& index);
    void onPullProgressUpdated(const QString& modelName, int percent, const QString& status);
    void onPullFinished(const QString& modelName);
    void onSendMessage();
    void onOllamaChunk(const QString& chunk);
    void onOllamaComplete(const QString& fullResponse);
    void onOllamaError(const QString& error);
    void onItemChanged(QStandardItem* item);
    void onChatNodeSelected(const QModelIndex& current, const QModelIndex& previous);
    void onActiveEndpointChanged(int index);
    void onConnectionStatusChanged(bool isOk);
    void onGenerationMetrics(double tokensPerSecond);
    void updateEndpointsList();
    void updateBreadcrumbs();
    void refreshVfsExplorer();
    void onBreadcrumbClicked(const QString& type, int id);
    void onRenameCurrentItem();
    void onDiscardChanges();
    void showBookContextMenu(const QPoint& pos);
    void showDocumentAIToolsMenu();
    void onDocumentAIOperations();
    void onDocumentHistory();
    void onEditDocument();
    void showItemContextMenu(QStandardItem* item, const QPoint& globalPos);
    void showOpenBookContextMenu(const QPoint& pos);
    void showVfsContextMenu(const QPoint& pos);
    void showInputSettingsMenu();
    void showChatSettingsDialog(int messageId);
    void updateInputBehavior();
    void exportChatSession();
    void importChatSession();
    void exportDocument(int id, const QString& type);
    void onOpenBooksSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected);
    void updateQueueStatus();
    void updateNotificationStatus();
    void showNotificationMenu();
    void showQueueWindow();
    void showSpyWindow();
    void onQueueItemClicked(std::shared_ptr<BookDatabase> db, int messageId);
    void updateTreeMarkersRecursive(QStandardItem* parent, const QList<Notification>& notifications);
    void updateVfsMarkers(const QList<Notification>& notifications);
    bool moveItemToFolder(QStandardItem* draggedItem, QStandardItem* targetItem, bool isCopy = false);
    void onQueueChunk(std::shared_ptr<BookDatabase> db, int messageId, const QString& chunk, const QString& targetType);
    void onProcessingStarted(std::shared_ptr<BookDatabase> db, int messageId, const QString& targetType);
    void onProcessingFinished(std::shared_ptr<BookDatabase> db, int messageId, bool success, const QString& targetType);

   protected:
    void closeEvent(QCloseEvent* event) override;

   private:
    void setupUi();
    void loadBooks();
    void setupWindow();
    void loadSession(int rootId);
    void populateTree(QStandardItem* parentItem, int parentId, const QList<MessageNode>& allMessages);
    void populateChatFolders(QStandardItem* parentItem, int folderId, const QList<MessageNode>& allMessages,
                             BookDatabase* db);
    void populateMessageForks(QStandardItem* parentItem, int parentId, const QList<MessageNode>& allMessages);
    void populateDocumentFolders(QStandardItem* parentItem, int folderId, const QString& type, BookDatabase* db);
    void addPhantomItem(QStandardItem* folderItem, const QString& type);
    void updateLinearChatView(int tailNodeId, const QList<MessageNode>& allMessages);
    void getPathToRoot(int nodeId, const QList<MessageNode>& allMessages, QList<MessageNode>& path);
    int getEndOfLinearPath(int startId, const QList<MessageNode>& allMessages, QList<MessageNode>& outChildren);
    QString getChatNodeTitle(int nodeId, const QList<MessageNode>& allMessages);
    void loadDocumentsAndNotes();
    QStandardItem* findItemById(QStandardItem* parent, int id);
    QStandardItem* findItemInTree(int id, const QString& type);
    QStandardItem* findItemRecursive(QStandardItem* parent, int id, const QString& type);

    QSplitter* splitter;
    QSplitter* leftSplitter;
    QTreeView* openBooksTree;
    CustomItemModel* openBooksModel;
    QListWidget* bookList;             // Closed books
    QStackedWidget* mainContentStack;  // To switch between main views
    QWidget* emptyView;
    QListView* vfsExplorer;
    CustomItemModel* vfsModel;
    QWidget* chatWindowView;
    QSplitter* chatSplitter;
    QListView* chatForkExplorer;
    QStandardItemModel* forkExplorerModel;
    QTreeView* chatTree;      // Keeping it for the folder view structure if needed
    QTextEdit* chatTextArea;  // Replaces linearChatList
    QWidget* chatInputContainer;
    QWidget* docContainer;
    QStackedWidget* documentStack;
    QTextEdit* documentEditorView;
    QTextBrowser* documentPreviewView;
    QPushButton* saveDocBtn;
    QPushButton* previewDocBtn;
    QPushButton* editDocBtn;

    QWidget* noteContainer;
    QTextEdit* noteEditorView;
    QPushButton* saveNoteBtn;

    QTextEdit* multiLineInput;
    QPushButton* toggleInputModeBtn;

    QStackedWidget* chatStackWidget;  // To switch between them

    QWidget* breadcrumbWidget;
    QHBoxLayout* breadcrumbLayout;

    // Drag & Drop helpers
    bool eventFilter(QObject* obj, QEvent* event) override;
    void handleBookDrop(const QString& fileName);
    void closeBook(const QString& fileName);
    QStandardItemModel* chatModel;
    ChatInputWidget* inputField;  // Using HEAD's custom input field
    QPushButton* sendButton;
    QPushButton* discardChangesBtn;
    QPushButton* modelSelectButton;
    QStringList m_availableModels;
    QString m_selectedModel;
    QToolButton* inputSettingsButton;
    QComboBox* modelComboBox;

    QMap<QString, std::shared_ptr<BookDatabase>> m_openDatabases;
    QMap<int, class DocumentEditWindow*> m_openDocEditors;
    std::shared_ptr<BookDatabase> currentDb;
    OllamaClient ollamaClient;

    int currentLastNodeId;  // ID of the last node in the current chat path
    int m_activeDraftNodeId = 0;
    int m_activeDraftVersion = 0;
    QList<MessageNode> currentChatPath;  // Stores the current path for tracking edits/forks

    int currentDocumentId = 0;
    int currentNoteId = 0;
    int currentAutoDraftId = 0;
    int currentChatFolderId = 0;

    QString m_newChatSystemPrompt;
    QString m_newChatSendBehavior = "default";
    QString m_newChatModel = "default";
    QString m_newChatMultiLine = "default";
    QString m_newChatDraftPrompt;
    QString m_newChatUserNote;

    bool isCreatingNewChat = false;
    bool isCreatingNewDoc = false;
    bool isCreatingNewNote = false;

    bool m_isGenerating = false;
    int m_generationId = 0;

    QStatusBar* statusBar;
    QLabel* statusLabel;
    QLabel* modelLabel;
    QComboBox* endpointComboBox;
    QLabel* connectionStatusLabel;

    QToolButton* queueStatusBtn;
    QToolButton* notificationBtn;
    QMenu* notificationMenu;

    QSystemTrayIcon* trayIcon;
    QToolButton* settingsStatusBarBtn;

    QPointer<QWidget> m_queueWindow;
    QPointer<QWidget> m_spyWindow;
};

#endif  // MAINWINDOW_H
