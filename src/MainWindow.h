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
#include <QStandardItemModel>
#include <QLineEdit>
#include <QInputDialog>
#include <QAction>
#include <memory>

#include "OllamaClient.h"
#include "BookDatabase.h"

class MainWindow : public KXmlGuiWindow {
    Q_OBJECT
   public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

   private slots:
    void onCreateBook();
    void onBookSelected(const QModelIndex& index);
    void onSendMessage();
    void onOllamaChunk(const QString& chunk);
    void onOllamaComplete(const QString& fullResponse);
    void onOllamaError(const QString& error);
    void onItemChanged(QStandardItem* item);
    void onChatNodeSelected(const QModelIndex& current, const QModelIndex& previous);

   private:
    void setupUi();
    void loadBooks();
    void setupWindow();
    void loadSession(int rootId);
    void populateTree(QStandardItem* parentItem, int parentId, const QList<MessageNode>& allMessages);
    QStandardItem* findItem(QStandardItem* parent, int id);

    QSplitter *splitter;
    QListWidget *bookList;
    QTreeView *chatTree;
    QStandardItemModel *chatModel;
    QLineEdit *inputField;
    QPushButton *sendButton;

    std::unique_ptr<BookDatabase> currentDb;
    OllamaClient ollamaClient;

    int currentLastNodeId; // ID of the last node in the current chat path
};

#endif  // MAINWINDOW_H
