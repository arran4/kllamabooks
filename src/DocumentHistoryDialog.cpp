#include "DocumentHistoryDialog.h"

#include <sqlite3.h>

#include <QApplication>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QTextEdit>
#include <QVBoxLayout>

#include "DocumentEditWindow.h"

DocumentHistoryDialog::DocumentHistoryDialog(std::shared_ptr<BookDatabase> db, int documentId, QWidget* parent)
    : QDialog(parent), m_db(db), m_documentId(documentId) {
    setWindowTitle(tr("Document History"));
    resize(700, 500);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);

    QWidget* leftWidget = new QWidget(this);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(new QLabel(tr("History Entries:")));
    m_historyList = new QListWidget(this);
    m_historyList->setMinimumWidth(200);
    leftLayout->addWidget(m_historyList);
    splitter->addWidget(leftWidget);

    QWidget* rightWidget = new QWidget(this);
    QVBoxLayout* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(new QLabel(tr("Content (Read-Only):")));
    m_contentView = new QTextEdit(this);
    m_contentView->setReadOnly(true);
    rightLayout->addWidget(m_contentView);
    splitter->addWidget(rightWidget);

    splitter->setSizes({200, 500});
    mainLayout->addWidget(splitter, 1);

    QHBoxLayout* bottomLayout = new QHBoxLayout();
    bottomLayout->addStretch();
    QPushButton* closeBtn = new QPushButton(tr("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    QPushButton* restoreBtn = new QPushButton(tr("Open Version in Editor"), this);
    connect(restoreBtn, &QPushButton::clicked, this, &DocumentHistoryDialog::onRestore);

    bottomLayout->addWidget(closeBtn);
    bottomLayout->addWidget(restoreBtn);
    mainLayout->addLayout(bottomLayout);

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

        auto docs = m_db->getDocuments();
        QString title;
        for (const auto& d : docs) {
            if (d.id == m_documentId) {
                title = d.title;
                break;
            }
        }

        // Open in a new DocumentEditWindow but load this specific version
        // Wait, DocumentEditWindow loads from DB. We can't pass a specific version directly via its constructor
        // unless we augment it, but the user requested: "You can open documents from the history this way"
        // Let's just create a new document with this content (fork it essentially) or let them edit it as a new draft.
        // Or better yet, we can augment DocumentEditWindow to accept initial content!

        // Actually, the simplest approach for "open version" is to create a new draft/fork with it.
        // Let's just fork it immediately and open that.
        int folderId = 0;
        for (const auto& d : docs) {
            if (d.id == m_documentId) {
                folderId = d.folderId;
                break;
            }
        }

        QDateTime dt = QDateTime::fromString(m_entries[idx].timestamp, Qt::ISODate);
        QString displayTime = dt.isValid() ? dt.toString("yyyy-MM-dd_HH-mm") : m_entries[idx].timestamp;

        int newId = m_db->addDocument(folderId, title + " (" + displayTime + ")", m_entries[idx].content, m_documentId);
        if (newId > 0) {
            DocumentEditWindow* editWin = new DocumentEditWindow(m_db, newId, title + " (" + displayTime + ")");

            // We need to wire it up to MainWindow to refresh tree.
            // We can emit a signal or invoke MainWindow.
            QWidget* mainWin = nullptr;
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                if (widget->inherits("MainWindow")) {
                    mainWin = widget;
                    break;
                }
            }
            if (mainWin) {
                QObject::connect(editWin, SIGNAL(documentModified(int)), mainWin, SLOT(loadDocumentsAndNotes()));
                QObject::connect(editWin, SIGNAL(newDocumentCreated(int)), mainWin, SLOT(loadDocumentsAndNotes()));
                // We should also trigger a tree reload immediately for the new fork
                QMetaObject::invokeMethod(mainWin, "loadDocumentsAndNotes");
            }

            editWin->show();
            accept();
        }
    }
}
