#include "QueueWindow.h"
#include <QHeaderView>
#include <QMessageBox>
#include <QFileInfo>

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
    m_cancelBtn = new QPushButton("Cancel", this);
    m_clearBtn = new QPushButton("Clear Completed", this);
    
    QPushButton* pauseBtn = new QPushButton(QueueManager::instance().isPaused() ? "Resume Queue" : "Pause Queue", this);
    
    btnLayout->addWidget(m_upBtn);
    btnLayout->addWidget(m_downBtn);
    btnLayout->addWidget(m_cancelBtn);
    btnLayout->addWidget(pauseBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(m_clearBtn);
    layout->addLayout(btnLayout);

    connect(m_cancelBtn, &QPushButton::clicked, this, &QueueWindow::onCancelItem);
    connect(m_clearBtn, &QPushButton::clicked, this, &QueueWindow::onClearCompleted);
    connect(pauseBtn, &QPushButton::clicked, this, [this, pauseBtn](){
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

    refresh();
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

void QueueWindow::onClearCompleted() {
    QueueManager::instance().clearCompleted();
}

void QueueWindow::onMoveUp() {
    // Priority reordering would go here
}

void QueueWindow::onMoveDown() {
}
