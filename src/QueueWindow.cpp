#include "QueueWindow.h"

#include <QFileInfo>
#include <QHeaderView>
#include <QInputDialog>
#include <QMessageBox>
#include <QTextEdit>

QueueWindow::QueueWindow(QWidget* parent) : QDialog(parent) {
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
    m_spyBtn = new QPushButton("Global Spy", this);
    m_clearBtn = new QPushButton("Clear Completed", this);

    QPushButton* pauseBtn = new QPushButton(QueueManager::instance().isPaused() ? "Resume Queue" : "Pause Queue", this);

    btnLayout->addWidget(m_upBtn);
    btnLayout->addWidget(m_downBtn);
    btnLayout->addWidget(m_cancelBtn);
    btnLayout->addWidget(m_retryBtn);
    btnLayout->addWidget(m_modifyBtn);
    btnLayout->addWidget(m_spyBtn);
    btnLayout->addWidget(pauseBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(m_clearBtn);
    layout->addLayout(btnLayout);

    connect(m_cancelBtn, &QPushButton::clicked, this, &QueueWindow::onCancelItem);
    connect(m_retryBtn, &QPushButton::clicked, this, &QueueWindow::onRetryItem);
    connect(m_modifyBtn, &QPushButton::clicked, this, &QueueWindow::onModifyItem);
    connect(m_spyBtn, &QPushButton::clicked, this, &QueueWindow::onSpyItem);
    connect(m_clearBtn, &QPushButton::clicked, this, &QueueWindow::onClearCompleted);
    connect(m_queueList, &QListWidget::itemSelectionChanged, this, &QueueWindow::updateButtons);
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
        return;
    }

    int id = item->data(Qt::UserRole).toInt();
    QString path = item->data(Qt::UserRole + 1).toString();

    bool isError = false;
    for (const auto& mi : QueueManager::instance().getMergedQueue()) {
        if (mi.item.id == id && mi.db->filepath() == path) {
            isError = (mi.item.status == "error");
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
        QString text = QString("[%1] %2: %3 (%4)")
                           .arg(mi.item.status.toUpper())
                           .arg(QFileInfo(mi.db->filepath()).fileName())
                           .arg(mi.item.prompt.left(100).replace("\n", " ").trimmed() + "...")
                           .arg(mi.item.model);

        QListWidgetItem* listItem = new QListWidgetItem(text, m_queueList);
        listItem->setData(Qt::UserRole, QVariant::fromValue(mi.item.id));
        listItem->setData(Qt::UserRole + 1, mi.db->filepath());

        if (mi.item.status == "processing") {
            listItem->setBackground(Qt::cyan);
        } else if (mi.item.status == "error") {
            listItem->setBackground(Qt::red);
        } else if (mi.item.status == "completed") {
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

void QueueWindow::onSpyItem() {
    QDialog* spyDialog = new QDialog(this);
    spyDialog->setWindowTitle("Global Spying");
    spyDialog->resize(500, 400);

    QVBoxLayout* layout = new QVBoxLayout(spyDialog);
    QTextEdit* textEdit = new QTextEdit(spyDialog);
    textEdit->setReadOnly(true);
    layout->addWidget(textEdit);

    // Initial check for currently processing items
    for (const auto& mi : QueueManager::instance().getMergedQueue()) {
        if (mi.item.status == "processing") {
            spyDialog->setWindowTitle("Global Spying: " + mi.item.model);

            QString currentContent;
            if (mi.item.targetType == "document") {
                auto docs = mi.db->getDocuments();
                for (const auto& d : docs) {
                    if (d.id == mi.item.messageId) {
                        currentContent = d.content;
                        break;
                    }
                }
            } else {
                auto messages = mi.db->getMessages();
                for (const auto& m : messages) {
                    if (m.id == mi.item.messageId) {
                        currentContent = m.content;
                        break;
                    }
                }
            }
            textEdit->setPlainText(currentContent);
            textEdit->moveCursor(QTextCursor::End);
            break; // Just show the first processing one we find
        }
    }

    connect(&QueueManager::instance(), &QueueManager::processingStarted, spyDialog,
            [spyDialog, textEdit](std::shared_ptr<BookDatabase> db, int messageId, const QString& type) {
                spyDialog->setWindowTitle("Global Spying (Generating...)");
                textEdit->clear();
            });

    connect(&QueueManager::instance(), &QueueManager::processingChunk, spyDialog,
            [textEdit](std::shared_ptr<BookDatabase> db, int mId, const QString& chunk, const QString& type) {
                textEdit->moveCursor(QTextCursor::End);
                textEdit->insertPlainText(chunk);
                textEdit->moveCursor(QTextCursor::End);
            });

    connect(&QueueManager::instance(), &QueueManager::processingFinished, spyDialog,
            [spyDialog](std::shared_ptr<BookDatabase> db, int mId, bool success, const QString& type) {
                spyDialog->setWindowTitle("Global Spying (Idle)");
            });

    spyDialog->setAttribute(Qt::WA_DeleteOnClose);
    spyDialog->show();
}

void QueueWindow::onClearCompleted() { QueueManager::instance().clearCompleted(); }

void QueueWindow::onMoveUp() {
    // Priority reordering would go here
}

void QueueWindow::onMoveDown() {}
