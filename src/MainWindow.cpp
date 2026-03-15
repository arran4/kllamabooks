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

MainWindow::MainWindow(QWidget *parent)
    : KXmlGuiWindow(parent),
      currentLastNodeId(0) {

    setupUi();
    setupWindow();
}

MainWindow::~MainWindow() {}

void MainWindow::setupUi() {
    splitter = new QSplitter(this);
    setCentralWidget(splitter);

    bookList = new QListWidget(this);
    splitter->addWidget(bookList);

    QWidget* rightWidget = new QWidget(this);
    QVBoxLayout* rightLayout = new QVBoxLayout(rightWidget);

    chatTree = new QTreeView(this);
    chatModel = new QStandardItemModel(this);
    chatTree->setModel(chatModel);
    chatTree->setHeaderHidden(true);
    chatTree->setEditTriggers(QAbstractItemView::DoubleClicked);
    rightLayout->addWidget(chatTree);

    inputField = new QLineEdit(this);
    sendButton = new QPushButton("Send", this);
    QHBoxLayout* inputLayout = new QHBoxLayout();
    inputLayout->addWidget(inputField);
    inputLayout->addWidget(sendButton);
    rightLayout->addLayout(inputLayout);

    splitter->addWidget(rightWidget);

    QToolBar* toolbar = addToolBar("Main");
    QAction* newBookAction = toolbar->addAction("New Book");

    connect(newBookAction, &QAction::triggered, this, &MainWindow::onCreateBook);
    connect(bookList, &QListWidget::clicked, this, &MainWindow::onBookSelected);
    connect(sendButton, &QPushButton::clicked, this, &MainWindow::onSendMessage);
    connect(inputField, &QLineEdit::returnPressed, this, &MainWindow::onSendMessage);

    connect(&ollamaClient, &OllamaClient::modelListUpdated, this, [](const QStringList& models){
        qDebug() << "Available models:" << models;
    });
    ollamaClient.fetchModels();
    loadBooks();
}

void MainWindow::setupWindow() {
    setWindowTitle("KLlamaBooks");
    resize(800, 600);
}

void MainWindow::loadBooks() {
    bookList->clear();
    QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    if(!dir.exists()) dir.mkpath(".");

    QStringList filters;
    filters << "*.db";
    QFileInfoList fileList = dir.entryInfoList(filters, QDir::Files);
    for(const QFileInfo& file : fileList) {
        bookList->addItem(file.fileName());
    }
}

void MainWindow::onCreateBook() {
    bool ok;
    QString bookName = QInputDialog::getText(this, "New Book", "Enter book name:", QLineEdit::Normal, "", &ok);
    if (ok && !bookName.isEmpty()) {
        QString password = QInputDialog::getText(this, "Password", "Enter password (optional):", QLineEdit::Password, "", &ok);
        if (ok) {
            QString fileName = bookName + ".db";
            WalletManager::savePassword(fileName, password);

            QString filePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/" + fileName;
            BookDatabase newDb(filePath);
            newDb.open(password); // Creates the schema

            loadBooks();
        }
    }
}

void MainWindow::onBookSelected(const QModelIndex& index) {
    QString fileName = bookList->item(index.row())->text();
    QString filePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/" + fileName;

    QString password = WalletManager::loadPassword(fileName);

    currentDb = std::make_unique<BookDatabase>(filePath);
    if (!currentDb->open(password)) {
        bool ok;
        password = QInputDialog::getText(this, "Unlock Book", "Enter password for " + fileName + ":", QLineEdit::Password, "", &ok);
        if (ok && currentDb->open(password)) {
            WalletManager::savePassword(fileName, password);
        } else {
            QMessageBox::warning(this, "Error", "Could not open book.");
            currentDb.reset();
            return;
        }
    }

    loadSession(0); // For now, load all as one session
}

void MainWindow::loadSession(int rootId) {
    chatModel->clear();
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

    ollamaClient.generate("llama2", text,
        [this, aiId, aiItem](const QString& chunk) {
            QString currentText = aiItem->text() + chunk;
            aiItem->setText(currentText);
        },
        [this, aiId, aiItem](const QString& /*full*/) {
            QString content = aiItem->text().mid(12); // remove "[assistant] "
            currentDb->updateMessage(aiId, content);
        },
        [this, aiItem](const QString& err) {
            aiItem->setText(aiItem->text() + " [ERROR: " + err + "]");
        }
    );
}

void MainWindow::onOllamaChunk(const QString& chunk) {}
void MainWindow::onOllamaComplete(const QString& fullResponse) {}
void MainWindow::onOllamaError(const QString& error) {}

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
        currentLastNodeId = item->data(Qt::UserRole).toInt();
    }
}
