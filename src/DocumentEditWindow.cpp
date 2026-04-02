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
                                       const QString& targetType,
                                       QWidget* parent)
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
    QAction* saveAction = new QAction(QIcon::fromTheme("document-save"), tr("Save"), this);
    connect(saveAction, &QAction::triggered, this, &DocumentEditWindow::onSaveClicked);

    QAction* saveAsAction = new QAction(QIcon::fromTheme("document-save-as"), tr("Save As..."), this);
    connect(saveAsAction, &QAction::triggered, this, &DocumentEditWindow::onSaveAsClicked);

    QAction* saveAsDraftAction = new QAction(QIcon::fromTheme("document-new"), tr("Save as Draft"), this);
    connect(saveAsDraftAction, &QAction::triggered, this, &DocumentEditWindow::onSaveAsDraftClicked);

    QAction* renameAction = new QAction(QIcon::fromTheme("edit-rename"), tr("Rename"), this);
    connect(renameAction, &QAction::triggered, this, &DocumentEditWindow::onRenameClicked);

    QAction* closeAction = new QAction(QIcon::fromTheme("window-close"), tr("Close"), this);
    connect(closeAction, &QAction::triggered, this, &QWidget::close);

    KToolBar* mainToolBar = new KToolBar("mainToolBar", this);
    mainToolBar->addAction(saveAction);
    mainToolBar->addAction(saveAsAction);
    mainToolBar->addAction(saveAsDraftAction);
    mainToolBar->addAction(renameAction);
    mainToolBar->addAction(closeAction);
    addToolBar(mainToolBar);

    QMenu* fileMenu = new QMenu(tr("File"), this);
    fileMenu->addAction(saveAction);
    fileMenu->addAction(saveAsAction);
    fileMenu->addAction(saveAsDraftAction);
    fileMenu->addSeparator();
    fileMenu->addAction(renameAction);
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
}

void DocumentEditWindow::updateStatusBar() {
    QDateTime currentDbTimestamp = getLatestDbTimestamp();
    if (currentDbTimestamp > m_openTimestamp) {
        m_statusLabel->setText(tr("⚠️ Must Fork - Document was modified externally"));
        m_statusLabel->setStyleSheet("color: red; font-weight: bold;");
    } else {
        m_statusLabel->setText(tr("Ready"));
        m_statusLabel->setStyleSheet("");
    }
}

void DocumentEditWindow::setInitialContent(const QString& content) {
    m_editor->setPlainText(content);
}

void DocumentEditWindow::loadDocument() {
    if (!m_db || !m_db->isOpen()) return;

    auto docs = m_db->getDocuments();
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
                                             m_title + " (Draft)", &ok);
    if (ok && !newTitle.isEmpty()) {
        int newId = saveToDraft(newTitle);
        if (newId > 0) {
            m_statusLabel->setText(tr("Saved to drafts"));
            emit newDocumentCreated(newId);
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

    auto docs = m_db->getDocuments();
    int folderId = 0;
    for (const auto& d : docs) {
        if (d.id == m_documentId) {
            folderId = d.folderId;
            break;
        }
    }

    return m_db->addDocument(folderId, newTitle, m_editor->toPlainText(), m_documentId);
}

int DocumentEditWindow::saveToDraft(const QString& newTitle) {
    if (!m_db || !m_db->isOpen()) return 0;

    auto docs = m_db->getDocuments();
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
    if (currentDbTimestamp > m_openTimestamp) {
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
