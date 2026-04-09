#include "DocumentEditWindow.h"

#include <sqlite3.h>

#include <KToolBar>
#include <QAction>
#include <QCloseEvent>
#include <QDateTime>
#include <QDebug>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QStatusBar>
#include <QTextEdit>
#include <QVBoxLayout>

DocumentEditWindow::DocumentEditWindow(std::shared_ptr<BookDatabase> db, int documentId, const QString& title,
                                       const QString& targetType, QWidget* parent)
    : KXmlGuiWindow(parent, Qt::Window), m_db(db), m_documentId(documentId), m_title(title), m_targetType(targetType) {
    setWindowTitle(tr("Editing: %1").arg(title));
    resize(800, 600);
    setAttribute(Qt::WA_DeleteOnClose);

    setupWindow();
    loadDocument();
}

DocumentEditWindow::~DocumentEditWindow() {}

void DocumentEditWindow::setupWindow() {
    QWidget* centralWidget = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(centralWidget);
    layout->setContentsMargins(0, 0, 0, 0);

    m_editor = new QTextEdit(this);
    layout->addWidget(m_editor);
    setCentralWidget(centralWidget);

    connect(m_editor, &QTextEdit::textChanged, this, &DocumentEditWindow::onContentChanged);

    // Toolbar actions
    QAction* saveAction = new QAction(QIcon::fromTheme("document-save"), m_targetType == "draft" ? tr("Update Draft") : tr("Save"), this);
    connect(saveAction, &QAction::triggered, this, &DocumentEditWindow::onSaveClicked);

    QAction* saveAsAction = new QAction(QIcon::fromTheme("document-save-as"), tr("Save As..."), this);
    connect(saveAsAction, &QAction::triggered, this, &DocumentEditWindow::onSaveAsClicked);

    QAction* saveAsDraftAction = new QAction(QIcon::fromTheme("document-new"), tr("Save as Draft"), this);
    connect(saveAsDraftAction, &QAction::triggered, this, &DocumentEditWindow::onSaveAsDraftClicked);

    QAction* replaceOriginalAction = nullptr;
    QAction* viewOriginalAction = nullptr;
    if (m_targetType == "draft") {
        saveAsDraftAction->setVisible(false); // Hide "Save as Draft" if it is already a draft.

        replaceOriginalAction = new QAction(QIcon::fromTheme("document-export"), tr("Replace Original Document"), this);
        connect(replaceOriginalAction, &QAction::triggered, this, [this]() {
            if (!m_db || !m_db->isOpen()) return;

            QList<DocumentNode> drafts = m_db->getDrafts(-1);
            int parentId = 0;
            QString targetType = "";
            for (const auto& d : drafts) {
                if (d.id == m_documentId) {
                    parentId = d.parentId;
                    targetType = d.targetType;
                    break;
                }
            }

            if (parentId <= 0) {
                QMessageBox::warning(this, tr("Not Found"), tr("Cannot find original document."));
                return;
            }

            if (targetType == "document") {
                m_db->updateDocument(parentId, m_title, m_editor->toPlainText());
                QMessageBox::information(this, tr("Success"), tr("Original document replaced successfully."));
            } else if (targetType == "template") {
                m_db->updateTemplate(parentId, m_title, m_editor->toPlainText());
                QMessageBox::information(this, tr("Success"), tr("Original template replaced successfully."));
            } else if (targetType == "note") {
                m_db->updateNote(parentId, m_title, m_editor->toPlainText());
                QMessageBox::information(this, tr("Success"), tr("Original note replaced successfully."));
            }

            // Delete the draft after replacing
            m_db->deleteDraft(m_documentId);
            emit documentModified(m_documentId); // Trigger a refresh
            close();
        });

        viewOriginalAction = new QAction(QIcon::fromTheme("view-preview"), tr("View Original Document"), this);
        connect(viewOriginalAction, &QAction::triggered, this, [this]() {
            if (!m_db || !m_db->isOpen()) return;

            QList<DocumentNode> drafts = m_db->getDrafts(-1);
            int parentId = 0;
            QString targetType = "";
            for (const auto& d : drafts) {
                if (d.id == m_documentId) {
                    parentId = d.parentId;
                    targetType = d.targetType;
                    break;
                }
            }

            if (parentId <= 0) {
                QMessageBox::warning(this, tr("Not Found"), tr("Cannot find original document."));
                return;
            }

            emit jumpToDocumentRequested(parentId); // Signal MainWindow to jump there, need to update param
        });
    }

    QAction* renameAction = new QAction(QIcon::fromTheme("edit-rename"), tr("Rename"), this);
    connect(renameAction, &QAction::triggered, this, &DocumentEditWindow::onRenameClicked);

    QAction* jumpAction = new QAction(QIcon::fromTheme("go-jump"), tr("Jump to Item"), this);
    connect(jumpAction, &QAction::triggered, this, &DocumentEditWindow::onJumpClicked);

    QAction* dismissDraftAction = nullptr;
    if (m_targetType == "draft") {
        dismissDraftAction = new QAction(QIcon::fromTheme("edit-delete"), tr("Dismiss Draft"), this);
        connect(dismissDraftAction, &QAction::triggered, this, [this]() {
            if (!m_db || !m_db->isOpen()) return;

            QMessageBox::StandardButton reply;
            reply = QMessageBox::question(this, tr("Dismiss Draft"), tr("Are you sure you want to dismiss this draft permanently?"),
                                          QMessageBox::Yes|QMessageBox::No);
            if (reply == QMessageBox::Yes) {
                m_db->deleteDraft(m_documentId);
                m_initialContent = m_editor->toPlainText(); // Prevent save prompt
                emit documentModified(m_documentId);
                close();
            }
        });
    }

    QAction* closeAction = new QAction(QIcon::fromTheme("window-close"), tr("Close"), this);
    connect(closeAction, &QAction::triggered, this, &QWidget::close);

    KToolBar* mainToolBar = new KToolBar("mainToolBar", this);
    mainToolBar->addAction(saveAction);
    mainToolBar->addAction(saveAsAction);
    if (m_targetType != "draft") {
        mainToolBar->addAction(saveAsDraftAction);
    } else {
        if (replaceOriginalAction) mainToolBar->addAction(replaceOriginalAction);
        if (viewOriginalAction) mainToolBar->addAction(viewOriginalAction);
        if (dismissDraftAction) mainToolBar->addAction(dismissDraftAction);
    }
    mainToolBar->addAction(renameAction);
    mainToolBar->addAction(jumpAction);
    mainToolBar->addAction(closeAction);
    addToolBar(mainToolBar);

    QMenu* fileMenu = new QMenu(tr("File"), this);
    fileMenu->addAction(saveAction);
    fileMenu->addAction(saveAsAction);
    if (m_targetType != "draft") {
        fileMenu->addAction(saveAsDraftAction);
    } else {
        if (replaceOriginalAction) fileMenu->addAction(replaceOriginalAction);
        if (viewOriginalAction) fileMenu->addAction(viewOriginalAction);
        if (dismissDraftAction) fileMenu->addAction(dismissDraftAction);
    }
    fileMenu->addSeparator();
    fileMenu->addAction(renameAction);
    fileMenu->addAction(jumpAction);
    fileMenu->addSeparator();
    fileMenu->addAction(closeAction);
    menuBar()->addMenu(fileMenu);

    // Status bar
    QStatusBar* sbar = statusBar();
    m_wordCountLabel = new QLabel(tr("Words: 0"), this);
    sbar->addPermanentWidget(m_wordCountLabel);

    m_statusLabel = new QLabel(tr("Ready"), this);
    sbar->addWidget(m_statusLabel);
}

void DocumentEditWindow::onContentChanged() {
    QString text = m_editor->toPlainText();
    int words = text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).size();
    m_wordCountLabel->setText(tr("Words: %1").arg(words));
    updateStatusBar();
}

void DocumentEditWindow::setInitialContent(const QString& content) {
    m_editor->setPlainText(content);
}

void DocumentEditWindow::updateStatusBar() {
    if (m_targetType == "draft") {
        m_statusLabel->setText(tr("Viewing Draft"));
        m_statusLabel->setStyleSheet("color: #FF8C00; font-weight: bold;"); // Dark Orange
        return;
    }

    QDateTime currentDbTimestamp = getLatestDbTimestamp();
    if (currentDbTimestamp > m_openTimestamp) {
        m_statusLabel->setText(tr("⚠️ Must Fork - Document was modified externally"));
        m_statusLabel->setStyleSheet("color: red; font-weight: bold;");
    } else if (m_editor->toPlainText() != m_initialContent) {
        m_statusLabel->setText(tr("Modified"));
        m_statusLabel->setStyleSheet("color: blue;");
    } else {
        m_statusLabel->setText(tr("Ready"));
        m_statusLabel->setStyleSheet("");
    }
}

void DocumentEditWindow::loadDocument() {
    if (!m_db || !m_db->isOpen()) return;

    QList<DocumentNode> docs;
    if (m_targetType == "document") docs = m_db->getDocuments();
    else if (m_targetType == "draft") docs = m_db->getDrafts();
    else if (m_targetType == "template") docs = m_db->getTemplates();
    else if (m_targetType == "note") {
        auto notes = m_db->getNotes();
        for (const auto& n : notes) {
            if (n.id == m_documentId) {
                m_initialContent = n.content;
                if (m_editor->toPlainText().isEmpty()) {
                    m_editor->setPlainText(m_initialContent);
                }
                break;
            }
        }
        return;
    }

    for (const auto& d : docs) {
        if (d.id == m_documentId) {
            m_initialContent = d.content;
            if (m_editor->toPlainText().isEmpty()) {
                m_editor->setPlainText(m_initialContent);
            }
            break;
        }
    }

    m_openTimestamp = getLatestDbTimestamp();
}

QDateTime DocumentEditWindow::getLatestDbTimestamp() const {
    if (!m_db || !m_db->isOpen()) return QDateTime::currentDateTime();

    QDateTime latest;
    QList<DocumentNode> docs;
    if (m_targetType == "document") docs = m_db->getDocuments();
    else if (m_targetType == "draft") docs = m_db->getDrafts();
    else if (m_targetType == "template") docs = m_db->getTemplates();
    else if (m_targetType == "note") {
        auto notes = m_db->getNotes();
        for (const auto& n : notes) {
            if (n.id == m_documentId) {
                latest = n.timestamp;
                break;
            }
        }
    }

    if (m_targetType != "note") {
        for (const auto& d : docs) {
            if (d.id == m_documentId) {
                latest = d.timestamp;
                break;
            }
        }
    }

    if (m_targetType == "document") {
        auto history = m_db->getDocumentHistory(m_documentId);
        if (!history.isEmpty()) {
            QDateTime histTs = QDateTime::fromString(history.first().timestamp, Qt::ISODate);
            if (histTs > latest) {
                latest = histTs;
            }
        }
    }

    // If invalid, fallback to current time
    if (!latest.isValid()) {
        latest = QDateTime::currentDateTime();
    }

    return latest;
}

void DocumentEditWindow::onJumpClicked() {
    emit jumpToDocumentRequested(m_documentId);
}

void DocumentEditWindow::onSaveClicked() {
    if (saveToDb()) {
        m_initialContent = m_editor->toPlainText();
        m_openTimestamp = QDateTime::currentDateTime();  // Update reference time to avoid immediate re-conflicts
        m_statusLabel->setText(tr("Saved"));
        updateStatusBar();
        emit documentModified(m_documentId);
    }
}

void DocumentEditWindow::onSaveAsClicked() {
    bool ok;
    QString newTitle =
        QInputDialog::getText(this, tr("Save As"), tr("New Title:"), QLineEdit::Normal, m_title + " (Copy)", &ok);
    if (ok && !newTitle.isEmpty()) {
        int newId = forkDocument(newTitle);
        if (newId > 0) {
            m_documentId = newId;
            m_title = newTitle;
            setWindowTitle(tr("Editing: %1").arg(m_title));
            m_initialContent = m_editor->toPlainText();
            m_openTimestamp = QDateTime::currentDateTime();
            m_statusLabel->setText(tr("Saved as new document"));
            updateStatusBar();
            emit newDocumentCreated(m_documentId);
        }
    }
}

void DocumentEditWindow::onSaveAsDraftClicked() {
    bool ok;
    QString newTitle = QInputDialog::getText(this, tr("Save As Draft"), tr("Draft Title:"), QLineEdit::Normal,
                                             m_title, &ok);
    if (ok && !newTitle.isEmpty()) {
        int newId = saveToDraft(newTitle);
        if (newId > 0) {
            m_statusLabel->setText(tr("Saved to drafts"));
            emit newDocumentCreated(newId);
            close(); // Close active window since it was saved as draft
        }
    }
}

void DocumentEditWindow::onRenameClicked() {
    bool ok;
    QString newTitle = QInputDialog::getText(this, tr("Rename"), tr("New Title:"), QLineEdit::Normal, m_title, &ok);
    if (ok && !newTitle.isEmpty()) {
        if (m_db && m_db->isOpen()) {
            m_db->updateDocument(m_documentId, newTitle, m_editor->toPlainText());
            m_title = newTitle;
            setWindowTitle(tr("Editing: %1").arg(m_title));
            m_statusLabel->setText(tr("Renamed"));
            emit documentModified(m_documentId);
        }
    }
}

int DocumentEditWindow::forkDocument(const QString& newTitle) {
    if (!m_db || !m_db->isOpen()) return 0;

    QList<DocumentNode> docs;
    if (m_targetType == "document") docs = m_db->getDocuments();
    else if (m_targetType == "draft") docs = m_db->getDrafts();
    else if (m_targetType == "template") docs = m_db->getTemplates();
    else if (m_targetType == "note") {
        auto notes = m_db->getNotes();
        int folderId = 0;
        for (const auto& n : notes) {
            if (n.id == m_documentId) {
                folderId = n.folderId;
                break;
            }
        }
        return m_db->addNote(folderId, newTitle, m_editor->toPlainText());
    }

    int folderId = 0;
    for (const auto& d : docs) {
        if (d.id == m_documentId) {
            folderId = d.folderId;
            break;
        }
    }

    if (m_targetType == "document") {
        return m_db->addDocument(folderId, newTitle, m_editor->toPlainText(), m_documentId);
    } else if (m_targetType == "draft") {
        return m_db->addDraft(folderId, newTitle, m_editor->toPlainText());
    } else if (m_targetType == "template") {
        return m_db->addTemplate(folderId, newTitle, m_editor->toPlainText());
    }
    return 0;
}

int DocumentEditWindow::saveToDraft(const QString& newTitle) {
    if (!m_db || !m_db->isOpen()) return 0;

    QList<DocumentNode> docs;
    if (m_targetType == "document") docs = m_db->getDocuments();
    else if (m_targetType == "draft") docs = m_db->getDrafts();
    else if (m_targetType == "template") docs = m_db->getTemplates();
    else if (m_targetType == "note") {
        auto notes = m_db->getNotes();
        int folderId = 0;
        for (const auto& n : notes) {
            if (n.id == m_documentId) {
                folderId = n.folderId;
                break;
            }
        }
        return m_db->addDraft(folderId, newTitle, m_editor->toPlainText(), m_documentId, m_targetType);
    }

    int folderId = 0;
    for (const auto& d : docs) {
        if (d.id == m_documentId) {
            folderId = d.folderId;
            break;
        }
    }

    return m_db->addDraft(folderId, newTitle, m_editor->toPlainText(), m_documentId, m_targetType);
}

bool DocumentEditWindow::saveToDb() {
    if (!m_db || !m_db->isOpen()) return false;

    // Check for conflicts
    QDateTime currentDbTimestamp = getLatestDbTimestamp();
    // Sometimes timestamps match exactly due to seconds resolution. We consider it modified if strictly >
    // m_openTimestamp Wait, let's just do a strict greater check

    // Ignore conflict checking if this is just a draft we are editing,
    // since the target's modifications shouldn't prevent saving the draft itself
    if (currentDbTimestamp > m_openTimestamp && m_targetType != "draft") {
        // Conflict
        QMessageBox msgBox(this);
        msgBox.setWindowTitle(tr("Conflict Detected"));
        msgBox.setText(
            tr("This document has been modified externally since you opened it.\nWould you like to Create a New Fork, "
               "Save to Drafts, or Cancel?"));

        QPushButton* forkBtn = msgBox.addButton(tr("Create New Fork"), QMessageBox::AcceptRole);
        QPushButton* draftBtn = msgBox.addButton(tr("Save to Drafts"), QMessageBox::AcceptRole);
        QPushButton* cancelBtn = msgBox.addButton(tr("Cancel"), QMessageBox::RejectRole);

        msgBox.exec();

        if (msgBox.clickedButton() == forkBtn) {
            int newId = forkDocument(m_title + " (Fork)");
            if (newId > 0) {
                emit newDocumentCreated(newId);
                return true;
            }
            return false;
        } else if (msgBox.clickedButton() == draftBtn) {
            int newId = saveToDraft(m_title + " (Draft)");
            if (newId > 0) {
                emit newDocumentCreated(newId);
                return true;
            }
            return false;
        } else {
            return false;  // Cancel
        }
    } else {
        // No conflict, save normally
        if (m_targetType == "document") {
            m_db->addDocumentHistory(m_documentId, "manual_edit_pre", m_initialContent);
            m_db->updateDocument(m_documentId, m_title, m_editor->toPlainText());
        } else if (m_targetType == "draft") {
            m_db->updateDraft(m_documentId, m_title, m_editor->toPlainText());
        } else if (m_targetType == "template") {
            m_db->updateTemplate(m_documentId, m_title, m_editor->toPlainText());
        } else if (m_targetType == "note") {
            m_db->updateNote(m_documentId, m_title, m_editor->toPlainText());
        }
        return true;
    }
}

void DocumentEditWindow::closeEvent(QCloseEvent* event) {
    if (m_editor->toPlainText() != m_initialContent) {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle(tr("Unsaved Changes"));

        // If it's already a draft, they can just save it or discard
        if (m_targetType == "draft") {
            // First check if the original document has changed
            int originalId = -1;
            QString targetType = "";
            QDateTime originalTimestamp;
            QList<DocumentNode> drafts = m_db->getDrafts(-1);
            for (const auto& d : drafts) {
                if (d.id == m_documentId) {
                    originalId = d.parentId;
                    targetType = d.targetType;
                    break;
                }
            }

            if (originalId > 0) {
                // Fetch the original's current timestamp
                QList<DocumentNode> originals;
                if (targetType == "document") originals = m_db->getDocuments(-1);
                else if (targetType == "template") originals = m_db->getTemplates(-1);

                for (const auto& doc : originals) {
                    if (doc.id == originalId) {
                        originalTimestamp = doc.timestamp;
                        break;
                    }
                }

                // If the original has changed since this draft was created, we should warn them.
                // We use m_openTimestamp as a proxy, as we load it when the draft is opened.
                // Wait, m_openTimestamp tracks when the *draft* was opened. We should really
                // compare against when the draft was created/last updated, but since we don't have
                // the original's timestamp at draft creation easily accessible without joining history,
                // we'll just check if the original changed since we opened the draft window.
                // It's a best-effort check given the schema.

                if (originalTimestamp > m_openTimestamp) {
                    msgBox.setText(tr("Warning: The original document has been modified since you started editing this draft.\nWould you still like to Save this draft, Discard your current edits, or Cancel?"));
                    QPushButton* saveBtn = msgBox.addButton(tr("Save Draft"), QMessageBox::AcceptRole);
                    QPushButton* discardBtn = msgBox.addButton(tr("Discard Edits"), QMessageBox::DestructiveRole);
                    QPushButton* cancelBtn = msgBox.addButton(tr("Cancel"), QMessageBox::RejectRole);

                    msgBox.exec();

                    if (msgBox.clickedButton() == saveBtn) {
                        if (saveToDb()) {
                            emit documentModified(m_documentId);
                            event->accept();
                        } else {
                            event->ignore();
                        }
                    } else if (msgBox.clickedButton() == discardBtn) {
                        event->accept();
                    } else {
                        event->ignore();
                    }
                    return;
                }
            }

            msgBox.setText(tr("You have unsaved changes to this draft.\nWould you like to Save, Discard, or Cancel?"));
            QPushButton* saveBtn = msgBox.addButton(tr("Save"), QMessageBox::AcceptRole);
            QPushButton* discardBtn = msgBox.addButton(tr("Discard"), QMessageBox::DestructiveRole);
            QPushButton* cancelBtn = msgBox.addButton(tr("Cancel"), QMessageBox::RejectRole);

            msgBox.exec();

            if (msgBox.clickedButton() == saveBtn) {
                if (saveToDb()) {
                    emit documentModified(m_documentId);
                    event->accept();
                } else {
                    event->ignore();
                }
            } else if (msgBox.clickedButton() == discardBtn) {
                event->accept();
            } else {
                event->ignore();
            }
            return;
        }

        msgBox.setText(tr("You have unsaved changes.\nWould you like to Save to Drafts, Discard, or Cancel?"));

        QPushButton* saveDraftBtn = msgBox.addButton(tr("Save to Drafts"), QMessageBox::AcceptRole);
        QPushButton* discardBtn = msgBox.addButton(tr("Discard"), QMessageBox::DestructiveRole);
        QPushButton* cancelBtn = msgBox.addButton(tr("Cancel"), QMessageBox::RejectRole);

        msgBox.exec();

        if (msgBox.clickedButton() == saveDraftBtn) {
            saveToDraft(m_title + " (Draft)");
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
