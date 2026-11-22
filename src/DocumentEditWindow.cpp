#include "DocumentEditWindow.h"

#include <sqlite3.h>

#include <KActionCollection>
#include <KStandardAction>
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

void DocumentEditWindow::setReadOnly(bool readOnly) {
    m_isReadOnly = readOnly;
    m_editor->setReadOnly(readOnly);
    for (QAction* action : m_editActions) {
        action->setVisible(!readOnly);
    }
    if (readOnly) {
        setWindowTitle(tr("Preview: %1").arg(m_title));
    } else {
        setWindowTitle(tr("Editing: %1").arg(m_title));
    }
}

void DocumentEditWindow::setupWindow() {
    QWidget* centralWidget = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(centralWidget);
    layout->setContentsMargins(0, 0, 0, 0);

    m_editor = new QTextEdit(this);
    layout->addWidget(m_editor);
    setCentralWidget(centralWidget);

    connect(m_editor, &QTextEdit::textChanged, this, &DocumentEditWindow::onContentChanged);

    // Toolbar actions
    QAction* saveAction = KStandardAction::save(this, &DocumentEditWindow::onSaveClicked, actionCollection());
    if (m_targetType == "draft") saveAction->setText(tr("Update Draft"));
    actionCollection()->addAction(QStringLiteral("file_save"), saveAction);

    QAction* saveAsAction = KStandardAction::saveAs(this, &DocumentEditWindow::onSaveAsClicked, actionCollection());
    actionCollection()->addAction(QStringLiteral("file_save_as"), saveAsAction);

    QAction* saveAsDraftAction = new QAction(QIcon::fromTheme("document-new"), tr("Save as Draft"), this);
    actionCollection()->addAction(QStringLiteral("document_save_as_draft"), saveAsDraftAction);
    connect(saveAsDraftAction, &QAction::triggered, this, &DocumentEditWindow::onSaveAsDraftClicked);

    QAction* replaceOriginalAction = nullptr;
    QAction* viewOriginalAction = nullptr;
    if (m_targetType == "draft") {
        saveAsDraftAction->setVisible(false);  // Hide "Save as Draft" if it is already a draft.

        replaceOriginalAction = new QAction(QIcon::fromTheme("document-export"), tr("Replace Original Document"), this);
        actionCollection()->addAction(QStringLiteral("replace_original"), replaceOriginalAction);
        connect(replaceOriginalAction, &QAction::triggered, this, [this]() {
            if (!m_db || !m_db->isOpen()) return;

            auto draftOpt = m_db->getDraft(m_documentId);
            if (!draftOpt.has_value() || draftOpt->parentId <= 0) {
                QMessageBox::warning(this, tr("Not Found"), tr("Cannot find original document."));
                return;
            }
            const int parentId = draftOpt->parentId;
            const QString targetType = draftOpt->targetType;

            QDateTime originalTimestamp;
            int parentFolderId = 0;
            if (targetType == "document") {
                if (auto orig = m_db->getDocument(parentId)) {
                    originalTimestamp = orig->timestamp;
                    parentFolderId = orig->folderId;
                }
            } else if (targetType == "template") {
                if (auto orig = m_db->getTemplate(parentId)) {
                    originalTimestamp = orig->timestamp;
                    parentFolderId = orig->folderId;
                }
            } else if (targetType == "note") {
                if (auto orig = m_db->getNote(parentId)) {
                    originalTimestamp = orig->timestamp;
                    parentFolderId = orig->folderId;
                }
            }

            if (originalTimestamp > m_openTimestamp) {
                QMessageBox msgBox(this);
                msgBox.setWindowTitle(tr("Conflict Detected"));
                msgBox.setText(
                    tr("Warning: The original document has drifted (been modified) since this draft was "
                       "created/opened.\nWhat would you like to do?"));

                QPushButton* viewDiffBtn = msgBox.addButton(tr("View Differences"), QMessageBox::ActionRole);
                QPushButton* continueBtn = msgBox.addButton(tr("Continue & Replace"), QMessageBox::AcceptRole);
                QPushButton* forkBtn = msgBox.addButton(tr("Create New Forked Document"), QMessageBox::AcceptRole);
                QPushButton* deleteDraftBtn =
                    msgBox.addButton(tr("Delete Draft & Cancel"), QMessageBox::DestructiveRole);
                QPushButton* cancelBtn = msgBox.addButton(tr("Cancel"), QMessageBox::RejectRole);
                Q_UNUSED(cancelBtn)

                msgBox.exec();

                if (msgBox.clickedButton() == viewDiffBtn) {
                    emit jumpToDocumentRequested(parentId);  // Jump to original document
                    return;                                  // abort replacement for now
                } else if (msgBox.clickedButton() == continueBtn) {
                    // fallthrough to regular replace logic
                } else if (msgBox.clickedButton() == forkBtn) {
                    bool ok;
                    QString newTitle = QInputDialog::getText(this, tr("Create Fork"), tr("New Title:"),
                                                             QLineEdit::Normal, m_title + " (Fork)", &ok);
                    if (ok && !newTitle.isEmpty()) {
                        if (targetType == "note") {
                            m_db->addNote(parentFolderId, newTitle, m_editor->toPlainText());
                        } else if (targetType == "document") {
                            m_db->addDocument(parentFolderId, newTitle, m_editor->toPlainText(), parentId);
                        } else if (targetType == "template") {
                            m_db->addTemplate(parentFolderId, newTitle, m_editor->toPlainText());
                        }
                        m_db->deleteDraft(m_documentId);
                        emit documentModified(m_documentId);
                        close();
                    }
                    return;
                } else if (msgBox.clickedButton() == deleteDraftBtn) {
                    m_db->deleteDraft(m_documentId);
                    emit documentModified(m_documentId);
                    close();
                    return;
                } else {
                    return;  // Cancel
                }
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
            emit documentModified(m_documentId);  // Trigger a refresh
            close();
        });

        viewOriginalAction = new QAction(QIcon::fromTheme("view-preview"), tr("View Original Document"), this);
        actionCollection()->addAction(QStringLiteral("view_original"), viewOriginalAction);
        connect(viewOriginalAction, &QAction::triggered, this, [this]() {
            if (!m_db || !m_db->isOpen()) return;

            auto draftOpt = m_db->getDraft(m_documentId);
            if (!draftOpt.has_value() || draftOpt->parentId <= 0) {
                QMessageBox::warning(this, tr("Not Found"), tr("Cannot find original document."));
                return;
            }

            emit jumpToDocumentRequested(draftOpt->parentId);
        });
    }

    QAction* addCommentAction = nullptr;
    if (m_targetType == "draft") {
        addCommentAction = new QAction(QIcon::fromTheme("view-pim-notes"), tr("Edit Draft Description"), this);
        actionCollection()->addAction(QStringLiteral("add_comment"), addCommentAction);
        connect(addCommentAction, &QAction::triggered, this, [this]() {
            if (!m_db || !m_db->isOpen()) return;

            QString existingComment = "";
            int commentId = -1;
            QList<CommentNode> comments = m_db->getComments("draft", m_documentId);
            if (!comments.isEmpty()) {
                existingComment = comments.first().content;
                commentId = comments.first().id;
            }

            bool ok;
            QString desc = QInputDialog::getMultiLineText(this, tr("Draft Description"),
                                                          tr("Enter a description to help identify this draft:"),
                                                          existingComment, &ok);
            if (ok) {
                if (commentId == -1 && !desc.isEmpty()) {
                    m_db->addComment("draft", m_documentId, desc);
                } else if (commentId != -1) {
                    if (desc.isEmpty()) {
                        m_db->deleteComment(commentId);
                    } else {
                        m_db->updateComment(commentId, desc);
                    }
                }
                emit documentModified(m_documentId);  // Force UI refresh
            }
        });
    }

    QAction* renameAction = new QAction(QIcon::fromTheme("edit-rename"), tr("Rename"), this);
    actionCollection()->addAction(QStringLiteral("rename_document"), renameAction);
    connect(renameAction, &QAction::triggered, this, &DocumentEditWindow::onRenameClicked);

    QAction* saveAsTemplateAction = new QAction(QIcon::fromTheme("document-save-as"), tr("Save as Template"), this);
    actionCollection()->addAction(QStringLiteral("document_save_as_template"), saveAsTemplateAction);
    connect(saveAsTemplateAction, &QAction::triggered, this, &DocumentEditWindow::onSaveAsTemplateClicked);

    m_editActions << saveAction << saveAsAction << saveAsDraftAction << renameAction;

    QAction* jumpAction = new QAction(QIcon::fromTheme("go-jump"), tr("Jump to Item"), this);
    actionCollection()->addAction(QStringLiteral("jump_to_item"), jumpAction);
    connect(jumpAction, &QAction::triggered, this, &DocumentEditWindow::onJumpClicked);

    QAction* dismissDraftAction = nullptr;
    if (m_targetType == "draft") {
        dismissDraftAction = new QAction(QIcon::fromTheme("edit-delete"), tr("Dismiss Draft"), this);
        actionCollection()->addAction(QStringLiteral("dismiss_draft"), dismissDraftAction);
        connect(dismissDraftAction, &QAction::triggered, this, [this]() {
            if (!m_db || !m_db->isOpen()) return;

            QMessageBox::StandardButton reply;
            reply = QMessageBox::question(this, tr("Dismiss Draft"),
                                          tr("Are you sure you want to dismiss this draft permanently?"),
                                          QMessageBox::Yes | QMessageBox::No);
            if (reply == QMessageBox::Yes) {
                m_db->deleteDraft(m_documentId);
                m_initialContent = m_editor->toPlainText();  // Prevent save prompt
                emit documentModified(m_documentId);
                close();
            }
        });
    }

    QAction* closeAction = KStandardAction::close(this, &QWidget::close, actionCollection());
    actionCollection()->addAction(QStringLiteral("file_close"), closeAction);

    setupGUI(Default, ":/kdocumenteditwindowui.rc");

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

void DocumentEditWindow::setInitialContent(const QString& content) { m_editor->setPlainText(content); }

void DocumentEditWindow::updateStatusBar() {
    if (m_targetType == "draft") {
        m_statusLabel->setText(tr("Viewing Draft"));
        m_statusLabel->setStyleSheet("color: #FF8C00; font-weight: bold;");  // Dark Orange
        return;
    }

    QDateTime currentDbTimestamp = getLatestDbTimestamp();
    int currentVersionId =
        (m_db && m_db->isOpen()) ? m_db->getArtifactCurrentVersionId(m_documentId, m_targetType) : m_loadedVersionId;
    if ((m_loadedVersionId > 0 && currentVersionId > 0 && currentVersionId != m_loadedVersionId) ||
        currentDbTimestamp > m_openTimestamp) {
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

    if (m_targetType == "note") {
        if (auto n = m_db->getNote(m_documentId)) {
            m_initialContent = n->content;
            if (m_editor->toPlainText().isEmpty()) {
                m_editor->setPlainText(m_initialContent);
            }
        }
        m_loadedVersionId = m_db->getArtifactCurrentVersionId(m_documentId, m_targetType);
        m_openTimestamp = getLatestDbTimestamp();
        return;
    }

    std::optional<DocumentNode> docOpt;
    if (m_targetType == "document")
        docOpt = m_db->getDocument(m_documentId);
    else if (m_targetType == "draft")
        docOpt = m_db->getDraft(m_documentId);
    else if (m_targetType == "template")
        docOpt = m_db->getTemplate(m_documentId);

    if (docOpt.has_value()) {
        m_initialContent = docOpt->content;
        if (m_editor->toPlainText().isEmpty()) {
            m_editor->setPlainText(m_initialContent);
        }
    }

    m_loadedVersionId = m_db->getArtifactCurrentVersionId(m_documentId, m_targetType);
    m_openTimestamp = getLatestDbTimestamp();
}

QDateTime DocumentEditWindow::getLatestDbTimestamp() const {
    if (!m_db || !m_db->isOpen()) return QDateTime::currentDateTime();

    QDateTime latest;
    if (m_targetType == "note") {
        if (auto n = m_db->getNote(m_documentId)) {
            latest = n->timestamp;
        }
    } else {
        std::optional<DocumentNode> docOpt;
        if (m_targetType == "document")
            docOpt = m_db->getDocument(m_documentId);
        else if (m_targetType == "draft")
            docOpt = m_db->getDraft(m_documentId);
        else if (m_targetType == "template")
            docOpt = m_db->getTemplate(m_documentId);

        if (docOpt.has_value()) {
            latest = docOpt->timestamp;
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

void DocumentEditWindow::onJumpClicked() { emit jumpToDocumentRequested(m_documentId); }

void DocumentEditWindow::onSaveClicked() {
    if (saveToDb()) {
        m_initialContent = m_editor->toPlainText();
        m_loadedVersionId = m_db->getArtifactCurrentVersionId(m_documentId, m_targetType);
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
            m_loadedVersionId = m_db->getArtifactCurrentVersionId(m_documentId, m_targetType);
            m_openTimestamp = QDateTime::currentDateTime();
            m_statusLabel->setText(tr("Saved as new document"));
            updateStatusBar();
            emit newDocumentCreated(m_documentId);
        }
    }
}

void DocumentEditWindow::onSaveAsTemplateClicked() {
    bool ok;
    QString newTitle = QInputDialog::getText(this, tr("Save As Template"), tr("Template Title:"), QLineEdit::Normal,
                                             m_title + " (Template)", &ok);
    if (ok && !newTitle.isEmpty()) {
        if (m_db && m_db->isOpen()) {
            int newId = m_db->addTemplate(0, newTitle, m_editor->toPlainText());
            if (newId > 0) {
                m_statusLabel->setText(tr("Saved to templates"));
                emit newDocumentCreated(newId);
            }
        }
    }
}

void DocumentEditWindow::onSaveAsDraftClicked() {
    bool ok;
    QString newTitle =
        QInputDialog::getText(this, tr("Save As Draft"), tr("Draft Title:"), QLineEdit::Normal, m_title, &ok);
    if (ok && !newTitle.isEmpty()) {
        int newId = saveToDraft(newTitle);
        if (newId > 0) {
            m_statusLabel->setText(tr("Saved to drafts"));
            emit newDocumentCreated(newId);
            close();  // Close active window since it was saved as draft
        }
    }
}

void DocumentEditWindow::onRenameClicked() {
    bool ok;
    QString newTitle = QInputDialog::getText(this, tr("Rename"), tr("New Title:"), QLineEdit::Normal, m_title, &ok);
    if (ok && !newTitle.isEmpty()) {
        if (m_db && m_db->isOpen()) {
            m_db->updateDocumentTitle(m_documentId, newTitle, m_targetType);
            m_title = newTitle;
            setWindowTitle(tr("Editing: %1").arg(m_title));
            m_statusLabel->setText(tr("Renamed"));
            emit documentModified(m_documentId);
        }
    }
}

int DocumentEditWindow::forkDocument(const QString& newTitle) {
    if (!m_db || !m_db->isOpen()) return 0;

    if (m_targetType == "note") {
        int folderId = 0;
        if (auto n = m_db->getNote(m_documentId)) {
            folderId = n->folderId;
        }
        return m_db->addNote(folderId, newTitle, m_editor->toPlainText());
    }

    int folderId = 0;
    std::optional<DocumentNode> docOpt;
    if (m_targetType == "document")
        docOpt = m_db->getDocument(m_documentId);
    else if (m_targetType == "draft")
        docOpt = m_db->getDraft(m_documentId);
    else if (m_targetType == "template")
        docOpt = m_db->getTemplate(m_documentId);
    if (docOpt.has_value()) {
        folderId = docOpt->folderId;
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

    if (m_targetType == "note") {
        int folderId = 0;
        if (auto n = m_db->getNote(m_documentId)) {
            folderId = n->folderId;
        }
        return m_db->addDraft(folderId, newTitle, m_editor->toPlainText(), m_documentId, m_targetType);
    }

    int folderId = 0;
    std::optional<DocumentNode> docOpt;
    if (m_targetType == "document")
        docOpt = m_db->getDocument(m_documentId);
    else if (m_targetType == "draft")
        docOpt = m_db->getDraft(m_documentId);
    else if (m_targetType == "template")
        docOpt = m_db->getTemplate(m_documentId);
    if (docOpt.has_value()) {
        folderId = docOpt->folderId;
    }

    return m_db->addDraft(folderId, newTitle, m_editor->toPlainText(), m_documentId, m_targetType);
}

bool DocumentEditWindow::saveToDb() {
    if (!m_db || !m_db->isOpen()) return false;

    QDateTime currentDbTimestamp = getLatestDbTimestamp();
    int currentVersionId = m_db->getArtifactCurrentVersionId(m_documentId, m_targetType);
    const bool hasConflict =
        ((m_loadedVersionId > 0 && currentVersionId > 0 && currentVersionId != m_loadedVersionId) ||
         currentDbTimestamp > m_openTimestamp) &&
        m_targetType != "draft";

    if (hasConflict) {
        // Conflict
        QMessageBox msgBox(this);
        msgBox.setWindowTitle(tr("Conflict Detected"));
        msgBox.setText(
            tr("This document has been modified externally since you opened it.\nWould you like to Create a New Fork, "
               "Save to Drafts, or Cancel?"));

        QPushButton* forkBtn = msgBox.addButton(tr("Create New Fork"), QMessageBox::AcceptRole);
        QPushButton* draftBtn = msgBox.addButton(tr("Save to Drafts"), QMessageBox::AcceptRole);
        QPushButton* cancelBtn = msgBox.addButton(tr("Cancel"), QMessageBox::RejectRole);
        Q_UNUSED(cancelBtn)

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
    if (!m_isReadOnly && m_editor->toPlainText() != m_initialContent) {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle(tr("Unsaved Changes"));

        // If it's already a draft, they can just save it or discard
        if (m_targetType == "draft") {
            // First check if the original document has changed
            int originalId = -1;
            QString targetType = "";
            QDateTime originalTimestamp;
            if (auto d = m_db->getDraft(m_documentId)) {
                originalId = d->parentId;
                targetType = d->targetType;
            }

            if (originalId > 0) {
                if (targetType == "document") {
                    if (auto doc = m_db->getDocument(originalId)) originalTimestamp = doc->timestamp;
                } else if (targetType == "template") {
                    if (auto tmpl = m_db->getTemplate(originalId)) originalTimestamp = tmpl->timestamp;
                } else if (targetType == "note") {
                    if (auto note = m_db->getNote(originalId)) originalTimestamp = note->timestamp;
                }

                // If the original has changed since this draft was created, we should warn them.
                // We use m_openTimestamp as a proxy, as we load it when the draft is opened.
                // Wait, m_openTimestamp tracks when the *draft* was opened. We should really
                // compare against when the draft was created/last updated, but since we don't have
                // the original's timestamp at draft creation easily accessible without joining history,
                // we'll just check if the original changed since we opened the draft window.
                // It's a best-effort check given the schema.

                if (originalTimestamp > m_openTimestamp) {
                    msgBox.setText(
                        tr("Warning: The original document has been modified since you started editing this "
                           "draft.\nWould you still like to Save this draft, Discard your current edits, or Cancel?"));
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

        msgBox.setText(tr("You have unsaved changes.\nWould you like to Save, Save to Drafts, Discard, or Cancel?"));

        QPushButton* saveBtn = msgBox.addButton(tr("Save"), QMessageBox::AcceptRole);
        QPushButton* saveDraftBtn = msgBox.addButton(tr("Save to Drafts"), QMessageBox::AcceptRole);
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
        } else if (msgBox.clickedButton() == saveDraftBtn) {
            int newId = saveToDraft(m_title);
            if (newId > 0) {
                emit newDocumentCreated(newId);
            }
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
