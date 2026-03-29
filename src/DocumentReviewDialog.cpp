#include "DocumentReviewDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <QInputDialog>
#include "QueueManager.h"

DocumentReviewDialog::DocumentReviewDialog(std::shared_ptr<BookDatabase> db, int queueItemId, QWidget* parent)
    : QDialog(parent), m_db(db), m_queueItemId(queueItemId), m_documentId(0) {

    setWindowTitle(tr("Review Generated Document changes"));
    resize(800, 600);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Prompt area
    mainLayout->addWidget(new QLabel(tr("Original Prompt:")));
    m_promptEdit = new QTextEdit(this);
    m_promptEdit->setMaximumHeight(150);
    mainLayout->addWidget(m_promptEdit);

    // Result area
    mainLayout->addWidget(new QLabel(tr("Generated Result (Editable):")));
    m_resultEdit = new QTextEdit(this);
    mainLayout->addWidget(m_resultEdit);

    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();

    QPushButton* replaceBtn = new QPushButton(tr("Replace Original"), this);
    QPushButton* appendBtn = new QPushButton(tr("Append to Original"), this);
    QPushButton* forkBtn = new QPushButton(tr("Create New Document"), this);
    QPushButton* regenerateBtn = new QPushButton(tr("Regenerate"), this);
    QPushButton* discardBtn = new QPushButton(tr("Discard"), this);

    btnLayout->addWidget(replaceBtn);
    btnLayout->addWidget(appendBtn);
    btnLayout->addWidget(forkBtn);
    btnLayout->addWidget(regenerateBtn);
    btnLayout->addWidget(discardBtn);

    mainLayout->addLayout(btnLayout);

    connect(replaceBtn, &QPushButton::clicked, this, &DocumentReviewDialog::onReplace);
    connect(appendBtn, &QPushButton::clicked, this, &DocumentReviewDialog::onAppend);
    connect(forkBtn, &QPushButton::clicked, this, &DocumentReviewDialog::onFork);
    connect(regenerateBtn, &QPushButton::clicked, this, &DocumentReviewDialog::onRegenerate);
    connect(discardBtn, &QPushButton::clicked, this, &DocumentReviewDialog::onDiscard);

    loadData();
}

DocumentReviewDialog::~DocumentReviewDialog() {}

void DocumentReviewDialog::loadData() {
    if (!m_db || !m_db->isOpen()) return;

    auto items = m_db->getQueue();
    for (const auto& item : items) {
        if (item.id == m_queueItemId) {
            m_documentId = item.messageId; // For document type, messageId is the docId
            m_promptEdit->setPlainText(item.prompt);
            m_resultEdit->setPlainText(item.response);
            break;
        }
    }
}

void DocumentReviewDialog::finalizeAndClose(bool deleteQueueItem) {
    if (deleteQueueItem && m_db && m_db->isOpen()) {
        m_db->deleteQueueItem(m_queueItemId);
    }
    accept();
}

void DocumentReviewDialog::onReplace() {
    if (!m_db || !m_db->isOpen()) return;

    QString newContent = m_resultEdit->toPlainText();

    // Fetch old document
    auto docs = m_db->getDocuments();
    QString title, oldContent;
    int folderId = 0;
    for (const auto& d : docs) {
        if (d.id == m_documentId) {
            title = d.title;
            oldContent = d.content;
            folderId = d.folderId;
            break;
        }
    }

    // Save to history
    m_db->addDocumentHistory(m_documentId, "replace_pre", oldContent);

    m_db->updateDocument(m_documentId, title, newContent);
    finalizeAndClose(true);
}

void DocumentReviewDialog::onAppend() {
    if (!m_db || !m_db->isOpen()) return;

    QString additionalContent = "\n" + m_resultEdit->toPlainText();

    auto docs = m_db->getDocuments();
    QString title, oldContent;
    for (const auto& d : docs) {
        if (d.id == m_documentId) {
            title = d.title;
            oldContent = d.content;
            break;
        }
    }

    m_db->addDocumentHistory(m_documentId, "append_pre", oldContent);

    m_db->updateDocument(m_documentId, title, oldContent + additionalContent);
    finalizeAndClose(true);
}

void DocumentReviewDialog::onFork() {
    if (!m_db || !m_db->isOpen()) return;

    auto docs = m_db->getDocuments();
    QString title;
    int folderId = 0;
    for (const auto& d : docs) {
        if (d.id == m_documentId) {
            title = d.title;
            folderId = d.folderId;
            break;
        }
    }

    bool ok;
    QString newTitle = QInputDialog::getText(this, tr("New Document Name"), tr("Title:"), QLineEdit::Normal, title + " (AI Fork)", &ok);
    if (ok && !newTitle.isEmpty()) {
        m_db->addDocument(folderId, newTitle, m_resultEdit->toPlainText(), m_documentId); // pass parentId to track lineage
        finalizeAndClose(true);
    }
}

void DocumentReviewDialog::onRegenerate() {
    if (!m_db || !m_db->isOpen()) return;

    QString newPrompt = m_promptEdit->toPlainText();

    m_db->updateQueueItemPrompt(m_queueItemId, newPrompt);
    m_db->updateQueueItemState(m_queueItemId, "pending", "");
    QueueManager::instance().checkQueue(); // trigger processing

    accept(); // Don't delete, we are regenerating it
}

void DocumentReviewDialog::onDiscard() {
    finalizeAndClose(true);
}
