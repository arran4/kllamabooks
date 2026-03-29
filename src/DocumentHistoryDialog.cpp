#include "DocumentHistoryDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QTextEdit>
#include <QLabel>
#include <QDateTime>
#include <sqlite3.h>
#include <QPushButton>

DocumentHistoryDialog::DocumentHistoryDialog(std::shared_ptr<BookDatabase> db, int documentId, QWidget* parent)
    : QDialog(parent), m_db(db), m_documentId(documentId) {

    setWindowTitle(tr("Document History"));
    resize(700, 500);

    QHBoxLayout* mainLayout = new QHBoxLayout(this);

    QVBoxLayout* leftLayout = new QVBoxLayout();
    leftLayout->addWidget(new QLabel(tr("History Entries:")));
    m_historyList = new QListWidget(this);
    m_historyList->setMinimumWidth(200);
    leftLayout->addWidget(m_historyList);

    QPushButton* closeBtn = new QPushButton(tr("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    leftLayout->addWidget(closeBtn);

    mainLayout->addLayout(leftLayout, 1);

    QVBoxLayout* rightLayout = new QVBoxLayout();
    rightLayout->addWidget(new QLabel(tr("Content (Read-Only):")));
    m_contentView = new QTextEdit(this);
    m_contentView->setReadOnly(true);
    rightLayout->addWidget(m_contentView, 3);

    QPushButton* restoreBtn = new QPushButton(tr("Restore Selected Version"), this);
    connect(restoreBtn, &QPushButton::clicked, this, &DocumentHistoryDialog::onRestore);
    rightLayout->addWidget(restoreBtn);

    mainLayout->addLayout(rightLayout, 3);

    connect(m_historyList, &QListWidget::itemSelectionChanged, this, &DocumentHistoryDialog::onSelectionChanged);

    loadHistory();
}

DocumentHistoryDialog::~DocumentHistoryDialog() {}

void DocumentHistoryDialog::loadHistory() {
    if (!m_db || !m_db->isOpen()) return;

    m_entries.clear();
    m_historyList->clear();

    // Use raw query for now since we haven't written a BookDatabase fetcher for history
    // Since we only need to read it, doing it here is a quick approach.
    // However, it's better to ask m_db for it.

    QList<HistoryEntry> entries;
    auto dbEntries = m_db->getDocumentHistory(m_documentId);
    for (const auto& dbe : dbEntries) {
        HistoryEntry e;
        e.id = dbe.id;
        e.actionType = dbe.actionType;
        e.content = dbe.content;
        e.timestamp = dbe.timestamp;
        entries.append(e);
    }

    m_entries = entries;
    for (const auto& e : m_entries) {
        QDateTime dt = QDateTime::fromString(e.timestamp, Qt::ISODate);
        QString displayTime = dt.isValid() ? dt.toString("yyyy-MM-dd HH:mm:ss") : e.timestamp;
        m_historyList->addItem(QString("%1: %2").arg(displayTime, e.actionType));
    }
}

void DocumentHistoryDialog::onSelectionChanged() {
    int idx = m_historyList->currentRow();
    if (idx >= 0 && idx < m_entries.size()) {
        m_contentView->setPlainText(m_entries[idx].content);
    } else {
        m_contentView->clear();
    }
}

void DocumentHistoryDialog::onRestore() {
    int idx = m_historyList->currentRow();
    if (idx >= 0 && idx < m_entries.size()) {
        if (!m_db || !m_db->isOpen()) return;

        // Save current state before restoring
        auto docs = m_db->getDocuments();
        QString title, oldContent;
        for (const auto& d : docs) {
            if (d.id == m_documentId) {
                title = d.title;
                oldContent = d.content;
                break;
            }
        }

        m_db->addDocumentHistory(m_documentId, "restore_pre", oldContent);

        // Update document with restored content
        m_db->updateDocument(m_documentId, title, m_entries[idx].content);

        // Notify user and close
        accept();
    }
}
