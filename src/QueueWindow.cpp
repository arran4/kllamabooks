#include "QueueWindow.h"

#include <QApplication>
#include <QFileInfo>
#include <QHeaderView>
#include <QInputDialog>
#include <QMessageBox>
#include <QTextEdit>

QueueWindow::QueueWindow(QWidget* parent) : QWidget(parent, Qt::Window) {
    setWindowTitle("LLM Request Queue");
    resize(600, 400);

    QVBoxLayout* layout = new QVBoxLayout(this);
    m_queueList = new QListWidget(this);
    layout->addWidget(new QLabel("Pending and Processing Tasks", this));
    layout->addWidget(m_queueList);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_upBtn = new QPushButton("Move Up", this);
    m_downBtn = new QPushButton("Move Down", this);
    m_cancelBtn = new QPushButton("Delete", this);
    m_retryBtn = new QPushButton("Retry", this);
    m_modifyBtn = new QPushButton("Modify", this);
    m_jumpBtn = new QPushButton("Jump", this);
    m_clearBtn = new QPushButton("Clear Completed", this);

    QPushButton* pauseBtn = new QPushButton(QueueManager::instance().isPaused() ? "Resume Queue" : "Pause Queue", this);

    btnLayout->addWidget(m_upBtn);
    btnLayout->addWidget(m_downBtn);
    btnLayout->addWidget(m_jumpBtn);
    btnLayout->addWidget(m_cancelBtn);
    btnLayout->addWidget(m_retryBtn);
    btnLayout->addWidget(m_modifyBtn);
    btnLayout->addWidget(pauseBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(m_clearBtn);
    layout->addLayout(btnLayout);

    connect(m_cancelBtn, &QPushButton::clicked, this, &QueueWindow::onCancelItem);
    connect(m_retryBtn, &QPushButton::clicked, this, &QueueWindow::onRetryItem);
    connect(m_modifyBtn, &QPushButton::clicked, this, &QueueWindow::onModifyItem);
    connect(m_jumpBtn, &QPushButton::clicked, this, &QueueWindow::onJumpItem);
    connect(m_clearBtn, &QPushButton::clicked, this, &QueueWindow::onClearCompleted);
    connect(m_queueList, &QListWidget::itemSelectionChanged, this, &QueueWindow::updateButtons);

    connect(m_queueList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        onJumpItem();
    });

    connect(pauseBtn, &QPushButton::clicked, this, [this, pauseBtn]() {
        if (QueueManager::instance().isPaused()) {
            QueueManager::instance().resumeQueue();
            pauseBtn->setText("Pause Queue");
        } else {
            QueueManager::instance().pauseQueue();
            pauseBtn->setText("Resume Queue");
        }
    });

    connect(&QueueManager::instance(), &QueueManager::queueChanged, this, &QueueWindow::refresh);

    // Disable up/down for now as it needs more logic for priority across DBs
    m_upBtn->setEnabled(false);
    m_downBtn->setEnabled(false);

    updateButtons();
    refresh();
}

void QueueWindow::updateButtons() {
    auto item = m_queueList->currentItem();
    if (!item) {
        m_retryBtn->setEnabled(false);
        m_modifyBtn->setEnabled(false);
        m_jumpBtn->setEnabled(false);
        return;
    }

    m_jumpBtn->setEnabled(true);

    int id = item->data(Qt::UserRole).toInt();
    QString path = item->data(Qt::UserRole + 1).toString();

    bool isError = false;
    for (const auto& mi : QueueManager::instance().getMergedQueue()) {
        if (mi.item.id == id && mi.db->filepath() == path) {
            isError = !mi.item.lastError.isEmpty();
            break;
        }
    }

    m_retryBtn->setEnabled(isError);
    m_modifyBtn->setEnabled(isError);
}

void QueueWindow::refresh() {
    m_queueList->clear();
    auto items = QueueManager::instance().getMergedQueue();
    for (const auto& mi : items) {
        QString statusStr = mi.item.state.toUpper();
        if (statusStr.isEmpty()) {
            statusStr = "PENDING";
            if (mi.item.processingId > 0)
                statusStr = "PROCESSING";
            else if (!mi.item.lastError.isEmpty())
                statusStr = "ERROR";
        }

        QString targetTitle = "Unknown";
        if (mi.item.targetType == "document") {
            auto docs = mi.db->getDocuments(-1);
            for (const auto& doc : docs) {
                if (doc.id == mi.item.messageId) {
                    targetTitle = doc.title;
                    break;
                }
            }
        } else if (mi.item.targetType == "message") {
            int rootId = mi.db->getRootMessageId(mi.item.messageId);
            ChatNode chat = mi.db->getChat(rootId);
            targetTitle = chat.title;
            if (targetTitle.isEmpty()) targetTitle = "Chat";
        }

        QString text = QString("[%1] %2 | %3: %4 | %5 (%6)")
                           .arg(statusStr)
                           .arg(QFileInfo(mi.db->filepath()).fileName())
                           .arg(mi.item.targetType.toUpper())
                           .arg(targetTitle)
                           .arg(mi.item.prompt.left(100).replace("\n", " ").trimmed() + "...")
                           .arg(mi.item.model);

        QListWidgetItem* listItem = new QListWidgetItem(text, m_queueList);
        listItem->setData(Qt::UserRole, QVariant::fromValue(mi.item.id));
        listItem->setData(Qt::UserRole + 1, mi.db->filepath());

        if (statusStr == "PROCESSING") {
            listItem->setBackground(Qt::cyan);
        } else if (statusStr == "ERROR") {
            listItem->setBackground(Qt::red);
        } else if (statusStr == "COMPLETED") {
            listItem->setBackground(Qt::green);
        }
    }
    updateButtons();
}

void QueueWindow::onRetryItem() {
    auto item = m_queueList->currentItem();
    if (!item) return;
    int id = item->data(Qt::UserRole).toInt();
    QString path = item->data(Qt::UserRole + 1).toString();

    for (auto db : QueueManager::instance().databases()) {
        if (db->filepath() == path) {
            QueueManager::instance().retryItem(db, id);
            break;
        }
    }
}

void QueueWindow::onModifyItem() {
    auto item = m_queueList->currentItem();
    if (!item) return;
    int id = item->data(Qt::UserRole).toInt();
    QString path = item->data(Qt::UserRole + 1).toString();

    for (auto db : QueueManager::instance().databases()) {
        if (db->filepath() == path) {
            // Find prompt
            QString oldPrompt;
            for (const auto& mi : QueueManager::instance().getMergedQueue()) {
                if (mi.item.id == id && mi.db == db) {
                    oldPrompt = mi.item.prompt;
                    break;
                }
            }

            bool ok;
            QString newPrompt = QInputDialog::getMultiLineText(this, "Modify Prompt", "Prompt:", oldPrompt, &ok);
            if (ok && !newPrompt.isEmpty()) {
                QueueManager::instance().modifyItem(db, id, newPrompt);
            }
            break;
        }
    }
}

void QueueWindow::onCancelItem() {
    auto item = m_queueList->currentItem();
    if (!item) return;
    int id = item->data(Qt::UserRole).toInt();
    QString path = item->data(Qt::UserRole + 1).toString();

    for (auto db : QueueManager::instance().databases()) {
        if (db->filepath() == path) {
            QueueManager::instance().cancelItem(db, id);
            break;
        }
    }
}

void QueueWindow::onClearCompleted() { QueueManager::instance().clearCompleted(); }

void QueueWindow::onMoveUp() {
    // Priority reordering would go here
}

void QueueWindow::onMoveDown() {}

void QueueWindow::onJumpItem() {
    auto item = m_queueList->currentItem();
    if (!item) return;

    int id = item->data(Qt::UserRole).toInt();
    QString path = item->data(Qt::UserRole + 1).toString();

    for (auto db : QueueManager::instance().databases()) {
        if (db->filepath() == path) {
            for (const auto& mi : QueueManager::instance().getMergedQueue()) {
                if (mi.item.id == id && mi.db == db) {
                    QWidget* mainWin = nullptr;
                    for (QWidget* widget : QApplication::topLevelWidgets()) {
                        if (widget->inherits("MainWindow")) {
                            mainWin = widget;
                            break;
                        }
                    }
                    if (mainWin) {
                        QMetaObject::invokeMethod(mainWin, "onQueueItemClicked",
                                                  Q_ARG(std::shared_ptr<BookDatabase>, db),
                                                  Q_ARG(int, mi.item.messageId));
                    }
                    break;
                }
            }
            break;
        }
    }
}
