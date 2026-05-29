#include "PromptHistoryDialog.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QTextEdit>
#include <QVBoxLayout>

PromptHistoryDialog::PromptHistoryDialog(std::shared_ptr<BookDatabase> db, int documentId, QWidget* parent)
    : QDialog(parent), m_db(db), m_documentId(documentId) {
    setWindowTitle(tr("Prompt History"));
    resize(700, 500);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);

    QWidget* leftWidget = new QWidget(this);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(new QLabel(tr("Prompt Entries:")));
    m_historyList = new QListWidget(this);
    m_historyList->setMinimumWidth(250);
    leftLayout->addWidget(m_historyList);
    splitter->addWidget(leftWidget);

    QWidget* rightWidget = new QWidget(this);
    QVBoxLayout* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(new QLabel(tr("Prompt Text (Read-Only):")));
    m_contentView = new QTextEdit(this);
    m_contentView->setReadOnly(true);
    rightLayout->addWidget(m_contentView);
    splitter->addWidget(rightWidget);

    splitter->setSizes({250, 450});
    mainLayout->addWidget(splitter, 1);

    QHBoxLayout* bottomLayout = new QHBoxLayout();
    bottomLayout->addStretch();
    QPushButton* closeBtn = new QPushButton(tr("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    bottomLayout->addWidget(closeBtn);
    mainLayout->addLayout(bottomLayout);

    connect(m_historyList, &QListWidget::itemSelectionChanged, this, &PromptHistoryDialog::onSelectionChanged);

    loadHistory();
}

PromptHistoryDialog::~PromptHistoryDialog() {}

void PromptHistoryDialog::loadHistory() {
    if (!m_db || !m_db->isOpen()) return;

    m_entries.clear();
    m_historyList->clear();

    m_entries = m_db->getPromptHistory(m_documentId);
    for (const auto& e : m_entries) {
        QDateTime dt = QDateTime::fromString(e.timestamp, "yyyy-MM-dd HH:mm:ss");
        QString displayTime = dt.isValid() ? dt.toString("yyyy-MM-dd HH:mm:ss") : e.timestamp;

        QString displayStatus = e.status;
        if (displayStatus.isEmpty()) displayStatus = "Completed/Removed";
        else if (displayStatus == "pending") displayStatus = "Queued";
        else if (displayStatus == "error") displayStatus = "Failed";

        m_historyList->addItem(QString("%1\n%2 (%3)").arg(displayTime, displayStatus, e.model));
    }
}

void PromptHistoryDialog::onSelectionChanged() {
    int idx = m_historyList->currentRow();
    if (idx >= 0 && idx < m_entries.size()) {
        m_contentView->setPlainText(m_entries[idx].prompt);
    } else {
        m_contentView->clear();
    }
}
