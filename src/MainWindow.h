#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <KXmlGuiWindow>
#include <QDateTime>
#include <QFutureWatcher>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStringList>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QSplitter>
#include <QListWidget>
#include <QTreeView>
#include <QListView>
#include <QTextEdit>
#include <QStandardItemModel>
#include <QLineEdit>
#include <QInputDialog>
#include <QAction>
#include <QComboBox>
#include <memory>

#include "OllamaClient.h"
#include "BookDatabase.h"
#include "ModelExplorer.h"
#include "SettingsDialog.h"

class MainWindow : public KXmlGuiWindow {
    Q_OBJECT
   public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

   private slots:
    void onCreateBook();
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
    void updateEndpointsList();
    void showBookContextMenu(const QPoint& pos);
    void showOpenBookContextMenu(const QPoint& pos);
    void showChatTreeContextMenu(const QPoint& pos);
    void showDocumentsContextMenu(const QPoint& pos);
    void showNotesContextMenu(const QPoint& pos);
    void onOpenBooksSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected);

   protected:
    void closeEvent(QCloseEvent *event) override;

   private:
    void setupUi();
    void loadBooks();
    void setupWindow();
    void loadSession(int rootId);
    void populateTree(QStandardItem* parentItem, int parentId, const QList<MessageNode>& allMessages);
    void populateChatFolders(QStandardItem* parentItem, int parentId, const QList<MessageNode>& allMessages);
    QStandardItem* findItem(QStandardItem* parent, int id);
    void updateLinearChatView(int tailNodeId, const QList<MessageNode>& allMessages);
    void getPathToRoot(int nodeId, const QList<MessageNode>& allMessages, QList<MessageNode>& path);
    void loadDocumentsAndNotes();

    QSplitter *splitter;
    QSplitter *leftSplitter;
    QTreeView *openBooksTree;
    QStandardItemModel *openBooksModel;
    QListWidget *bookList; // Closed books
    QStackedWidget *mainContentStack; // To switch between main views
    QWidget *emptyView;
    QListView *dbDirectView;
    QStandardItemModel *dbDirectModel;
    QTreeView *chatsFolderView; // Replaces chatTree functionality directly or wraps it
    QTreeView *documentsFolderView;
    QStandardItemModel *documentsModel;
    QTreeView *notesFolderView;
    QStandardItemModel *notesModel;
    QWidget *chatWindowView;
    QTreeView *chatTree; // Keeping it for the folder view structure if needed
    QTextEdit *chatTextArea; // Replaces linearChatList
    QWidget *docContainer;
    QTextEdit *documentEditorView;
    QPushButton *saveDocBtn;
    QWidget *noteContainer;
    QTextEdit *noteEditorView;
    QPushButton *saveNoteBtn;

    QStackedWidget *inputModeStack;
    QTextEdit *multiLineInput;
    QPushButton *toggleInputModeBtn;

    // Drag & Drop helpers
    bool eventFilter(QObject* obj, QEvent* event) override;
    void handleBookDrop(const QString& fileName);
    void closeBook(const QString& fileName);
    QStandardItemModel *chatModel;
    QLineEdit *inputField;
    QPushButton *sendButton;
    QPushButton *saveEditsBtn;
    QComboBox *modelComboBox;

    std::unique_ptr<BookDatabase> currentDb;
    OllamaClient ollamaClient;

    int currentLastNodeId; // ID of the last node in the current chat path
    QList<MessageNode> currentChatPath; // Stores the current path for tracking edits/forks

    int currentDocumentId = 0;
    int currentNoteId = 0;

    bool m_isGenerating = false;
    int m_generationId = 0;

    QStatusBar *statusBar;
    QLabel *statusLabel;
    QLabel *modelLabel;
    QComboBox *endpointComboBox;
    QLabel *connectionStatusLabel;
};

#endif  // MAINWINDOW_H
