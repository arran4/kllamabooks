#include "QueueWindow.h"

#include <QApplication>
#include <QFileInfo>
#include <QHeaderView>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QTextEdit>
#include <QFormLayout>

QueueWindow::QueueWindow(QWidget* parent) : QWidget(parent, Qt::Window) {
    setWindowTitle("LLM Request Queue");
    resize(600, 400);

    QVBoxLayout* layout = new QVBoxLayout(this);
    m_queueList = new QListWidget(this);
    layout->addWidget(new QLabel("Pending and Processing Tasks", this));
    layout->addWidget(m_queueList);

    m_queueList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_queueList, &QWidget::customContextMenuRequested, this, &QueueWindow::showContextMenu);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_upBtn = new QPushButton("Move Up", this);
    m_downBtn = new QPushButton("Move Down", this);
    m_clearBtn = new QPushButton("Clear Completed", this);

    QPushButton* pauseBtn = new QPushButton(QueueManager::instance().isPaused() ? "Resume Queue" : "Pause Queue", this);

    btnLayout->addWidget(m_upBtn);
    btnLayout->addWidget(m_downBtn);
    btnLayout->addWidget(pauseBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(m_clearBtn);
    layout->addLayout(btnLayout);

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
    // Only used to update bulk buttons like up/down now if we enable them
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

        if (statusStr == "PENDING" && !QueueManager::instance().isEndpointUp()) {
            statusStr = "ENDPOINT DOWN";
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

        if (statusStr == "ERROR" && !mi.item.lastError.isEmpty()) {
            text += QString(" - Error: %1").arg(mi.item.lastError);
        }

        QListWidgetItem* listItem = new QListWidgetItem(text, m_queueList);
        listItem->setData(Qt::UserRole, QVariant::fromValue(mi.item.id));
        listItem->setData(Qt::UserRole + 1, mi.db->filepath());

        if (statusStr == "PROCESSING") {
            listItem->setBackground(Qt::cyan);
        } else if (statusStr == "ERROR") {
            listItem->setBackground(Qt::red);
        } else if (statusStr == "COMPLETED") {
            listItem->setBackground(Qt::green);
        } else if (statusStr == "ENDPOINT DOWN") {
            listItem->setBackground(Qt::yellow);
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

void QueueWindow::showContextMenu(const QPoint& pos) {
    auto item = m_queueList->itemAt(pos);
    if (!item) return;

    int id = item->data(Qt::UserRole).toInt();
    QString path = item->data(Qt::UserRole + 1).toString();

    bool isError = false;
    for (const auto& mi : QueueManager::instance().getMergedQueue()) {
        if (mi.item.id == id && mi.db->filepath() == path) {
            isError = !mi.item.lastError.isEmpty();
            break;
        }
    }

    QMenu menu(this);
    menu.addAction("Show Details...", this, &QueueWindow::onShowDetails);
    menu.addAction("Jump to Target", this, &QueueWindow::onJumpItem);
    menu.addSeparator();

    QAction* modifyAction = menu.addAction("Modify Prompt...", this, &QueueWindow::onModifyItem);
    modifyAction->setEnabled(isError);

    QAction* retryAction = menu.addAction("Retry", this, &QueueWindow::onRetryItem);
    retryAction->setEnabled(isError);

    menu.addSeparator();
    menu.addAction("Delete", this, &QueueWindow::onCancelItem);

    menu.exec(m_queueList->mapToGlobal(pos));
}

void QueueWindow::onShowDetails() {
    auto item = m_queueList->currentItem();
    if (!item) return;

    int id = item->data(Qt::UserRole).toInt();
    QString path = item->data(Qt::UserRole + 1).toString();

    for (auto db : QueueManager::instance().databases()) {
        if (db->filepath() == path) {
            for (const auto& mi : QueueManager::instance().getMergedQueue()) {
                if (mi.item.id == id && mi.db == db) {
                    QDialog dlg(this);
                    dlg.setWindowTitle("Queue Item Details");
                    dlg.resize(500, 400);

                    QVBoxLayout* layout = new QVBoxLayout(&dlg);
                    QFormLayout* form = new QFormLayout();

                    form->addRow("Database:", new QLabel(QFileInfo(db->filepath()).fileName()));
                    form->addRow("State:", new QLabel(mi.item.state.toUpper()));
                    form->addRow("Model:", new QLabel(mi.item.model));
                    form->addRow("Target Type:", new QLabel(mi.item.targetType));
                    form->addRow("Message/Doc ID:", new QLabel(QString::number(mi.item.messageId)));
                    form->addRow("Priority:", new QLabel(QString::number(mi.item.priority)));

                    layout->addLayout(form);

                    if (!mi.item.lastError.isEmpty()) {
                        layout->addWidget(new QLabel("Error:"));
                        QTextEdit* errorEdit = new QTextEdit(mi.item.lastError);
                        errorEdit->setReadOnly(true);
                        errorEdit->setStyleSheet("color: red;");
                        errorEdit->setMaximumHeight(60);
                        layout->addWidget(errorEdit);
                    }

                    layout->addWidget(new QLabel("Prompt:"));
                    QTextEdit* promptEdit = new QTextEdit(mi.item.prompt);
                    promptEdit->setReadOnly(true);
                    layout->addWidget(promptEdit);

                    if (!mi.item.response.isEmpty()) {
                        layout->addWidget(new QLabel("Response (so far):"));
                        QTextEdit* responseEdit = new QTextEdit(mi.item.response);
                        responseEdit->setReadOnly(true);
                        layout->addWidget(responseEdit);
                    }

                    QPushButton* closeBtn = new QPushButton("Close");
                    layout->addWidget(closeBtn);
                    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);

                    dlg.exec();
                    break;
                }
            }
            break;
        }
    }
}

void QueueWindow::onJumpItem() {
    auto item = m_queueList->currentItem();
    if (!item) return;

    int id = item->data(Qt::UserRole).toInt();
    QString path = item->data(Qt::UserRole + 1).toString();

    for (auto db : QueueManager::instance().databases()) {
        if (db->filepath() == path) {
            for (const auto& mi : QueueManager::instance().getMergedQueue()) {
                if (mi.item.id == id && mi.db == db) {
                    // Validate if the target item still exists
                    bool isValid = false;
                    if (mi.item.targetType == "document") {
                        auto docs = mi.db->getDocuments(-1);
                        for (const auto& doc : docs) {
                            if (doc.id == mi.item.messageId) {
                                isValid = true;
                                break;
                            }
                        }
                    } else if (mi.item.targetType == "message") {
                        auto msgs = mi.db->getMessages();
                        for (const auto& msg : msgs) {
                            if (msg.id == mi.item.messageId) {
                                isValid = true;
                                break;
                            }
                        }
                    }

                    if (!isValid) return;

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
