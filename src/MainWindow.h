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
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QStringList>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <memory>

#include "BookDatabase.h"
#include "ModelExplorer.h"
#include "OllamaClient.h"
#include "SettingsDialog.h"

class MainWindow : public KXmlGuiWindow {
    Q_OBJECT
   public:
    explicit MainWindow(QWidget* parent = nullptr);
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
    void showLinearChatContextMenu(const QPoint& pos);
    void showChatTreeContextMenu(const QPoint& pos);

   protected:
    void closeEvent(QCloseEvent* event) override;

   private:
    void setupUi();
    void loadBooks();
    void setupWindow();
    void loadSession(int rootId);
    void populateTree(QStandardItem* parentItem, int parentId, const QList<MessageNode>& allMessages);
    QStandardItem* findItem(QStandardItem* parent, int id);
    void updateLinearChatView(int tailNodeId, const QList<MessageNode>& allMessages);
    void getPathToRoot(int nodeId, const QList<MessageNode>& allMessages, QList<MessageNode>& path);

    QSplitter* splitter;
    QSplitter* leftSplitter;
    QTreeView* openBooksTree;
    QStandardItemModel* openBooksModel;
    QListWidget* bookList;            // Closed books
    QTreeView* chatTree;              // The full branching tree
    QListWidget* linearChatList;      // The linear view
    QStackedWidget* chatStackWidget;  // To switch between them

    // Drag & Drop helpers
    bool eventFilter(QObject* obj, QEvent* event) override;
    void handleBookDrop(const QString& fileName);
    void closeBook(const QString& fileName);
    QStandardItemModel* chatModel;
    QLineEdit* inputField;
    QPushButton* sendButton;
    QComboBox* modelComboBox;

    std::unique_ptr<BookDatabase> currentDb;
    OllamaClient ollamaClient;

    int currentLastNodeId;  // ID of the last node in the current chat path

    QStatusBar* statusBar;
    QLabel* statusLabel;
    QLabel* modelLabel;
    QComboBox* endpointComboBox;
    QLabel* connectionStatusLabel;
};

#endif  // MAINWINDOW_H
