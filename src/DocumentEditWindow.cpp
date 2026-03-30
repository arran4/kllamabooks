#include "DocumentEditWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QPushButton>
#include <QMessageBox>
#include <QCloseEvent>
#include <QDateTime>
#include <sqlite3.h>
#include <QDebug>

DocumentEditWindow::DocumentEditWindow(std::shared_ptr<BookDatabase> db, int documentId, const QString& title, QWidget* parent)
    : QWidget(parent, Qt::Window), m_db(db), m_documentId(documentId), m_title(title) {

    setWindowTitle(tr("Editing: %1").arg(title));
    resize(800, 600);
    setAttribute(Qt::WA_DeleteOnClose);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    m_editor = new QTextEdit(this);
    mainLayout->addWidget(m_editor, 1);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    m_saveBtn = new QPushButton(tr("Save Changes"), this);

    btnLayout->addWidget(m_cancelBtn);
    btnLayout->addWidget(m_saveBtn);
    mainLayout->addLayout(btnLayout);

    connect(m_cancelBtn, &QPushButton::clicked, this, &DocumentEditWindow::onCancelClicked);
    connect(m_saveBtn, &QPushButton::clicked, this, &DocumentEditWindow::onSaveClicked);

    loadDocument();
}

DocumentEditWindow::~DocumentEditWindow() {}

void DocumentEditWindow::loadDocument() {
    if (!m_db || !m_db->isOpen()) return;

    auto docs = m_db->getDocuments();
    for (const auto& d : docs) {
        if (d.id == m_documentId) {
            m_initialContent = d.content;
            m_editor->setPlainText(m_initialContent);
            break;
        }
    }

    m_openTimestamp = getLatestDbTimestamp();
}

QDateTime DocumentEditWindow::getLatestDbTimestamp() const {
    if (!m_db || !m_db->isOpen()) return QDateTime::currentDateTime();

    QDateTime latest;
    // We do not have direct access to m_db->m_db. Let's use getDocuments().
    auto docs = m_db->getDocuments();
    for (const auto& d : docs) {
        if (d.id == m_documentId) {
            latest = d.timestamp;
            break;
        }
    }

    // Check document history
    auto history = m_db->getDocumentHistory(m_documentId);
    if (!history.isEmpty()) {
        QDateTime histTs = QDateTime::fromString(history.first().timestamp, Qt::ISODate);
        if (histTs > latest) {
            latest = histTs;
        }
    }

    // If invalid, fallback to current time
    if (!latest.isValid()) {
        latest = QDateTime::currentDateTime();
    }

    return latest;
}

void DocumentEditWindow::onSaveClicked() {
    if (saveToDb()) {
        m_initialContent = m_editor->toPlainText();
        m_openTimestamp = QDateTime::currentDateTime(); // Update reference time to avoid immediate re-conflicts
        QMessageBox::information(this, tr("Saved"), tr("Document saved successfully."));
    }
}

void DocumentEditWindow::onCancelClicked() {
    close();
}

int DocumentEditWindow::forkDocument() {
    if (!m_db || !m_db->isOpen()) return 0;

    auto docs = m_db->getDocuments();
    int folderId = 0;
    for (const auto& d : docs) {
        if (d.id == m_documentId) {
            folderId = d.folderId;
            break;
        }
    }

    return m_db->addDocument(folderId, m_title + " (Fork)", m_editor->toPlainText(), m_documentId);
}

int DocumentEditWindow::saveToDraft() {
    if (!m_db || !m_db->isOpen()) return 0;

    auto docs = m_db->getDocuments();
    int folderId = 0;
    for (const auto& d : docs) {
        if (d.id == m_documentId) {
            folderId = d.folderId;
            break;
        }
    }

    return m_db->addDraft(folderId, m_title + " (Draft)", m_editor->toPlainText());
}

bool DocumentEditWindow::saveToDb() {
    if (!m_db || !m_db->isOpen()) return false;

    // Check for conflicts
    QDateTime currentDbTimestamp = getLatestDbTimestamp();
    // Sometimes timestamps match exactly due to seconds resolution. We consider it modified if strictly > m_openTimestamp
    // Wait, let's just do a strict greater check
    if (currentDbTimestamp > m_openTimestamp) {
        // Conflict
        QMessageBox msgBox(this);
        msgBox.setWindowTitle(tr("Conflict Detected"));
        msgBox.setText(tr("This document has been modified externally since you opened it.\nWould you like to Create a New Fork, Save to Drafts, or Cancel?"));

        QPushButton* forkBtn = msgBox.addButton(tr("Create New Fork"), QMessageBox::AcceptRole);
        QPushButton* draftBtn = msgBox.addButton(tr("Save to Drafts"), QMessageBox::AcceptRole);
        QPushButton* cancelBtn = msgBox.addButton(tr("Cancel"), QMessageBox::RejectRole);

        msgBox.exec();

        if (msgBox.clickedButton() == forkBtn) {
            if (forkDocument() > 0) {
                return true;
            }
            return false;
        } else if (msgBox.clickedButton() == draftBtn) {
            if (saveToDraft() > 0) {
                return true;
            }
            return false;
        } else {
            return false; // Cancel
        }
    } else {
        // No conflict, save normally
        m_db->addDocumentHistory(m_documentId, "manual_edit_pre", m_initialContent);
        m_db->updateDocument(m_documentId, m_title, m_editor->toPlainText());
        return true;
    }
}

void DocumentEditWindow::closeEvent(QCloseEvent* event) {
    if (m_editor->toPlainText() != m_initialContent) {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle(tr("Unsaved Changes"));
        msgBox.setText(tr("You have unsaved changes.\nWould you like to Save to Drafts, Discard, or Cancel?"));

        QPushButton* saveDraftBtn = msgBox.addButton(tr("Save to Drafts"), QMessageBox::AcceptRole);
        QPushButton* discardBtn = msgBox.addButton(tr("Discard"), QMessageBox::DestructiveRole);
        QPushButton* cancelBtn = msgBox.addButton(tr("Cancel"), QMessageBox::RejectRole);

        msgBox.exec();

        if (msgBox.clickedButton() == saveDraftBtn) {
            saveToDraft();
            event->accept();
        } else if (msgBox.clickedButton() == discardBtn) {
            event->accept();
        } else {
            event->ignore();
        }
    } else {
        event->accept();
    }
}
