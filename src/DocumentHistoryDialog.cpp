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

    QPushButton* restoreInPlaceBtn = new QPushButton(tr("Restore Version"), this);
    connect(restoreInPlaceBtn, &QPushButton::clicked, this, &DocumentHistoryDialog::onRestoreInPlace);

    QPushButton* restoreBtn = new QPushButton(tr("Open Version in Editor"), this);
    connect(restoreBtn, &QPushButton::clicked, this, &DocumentHistoryDialog::onRestore);

    bottomLayout->addWidget(closeBtn);
    bottomLayout->addWidget(restoreInPlaceBtn);
    bottomLayout->addWidget(restoreBtn);
    mainLayout->addLayout(bottomLayout);

    connect(m_historyList, &QListWidget::itemSelectionChanged, this, &DocumentHistoryDialog::onSelectionChanged);

    loadHistory();
}

DocumentHistoryDialog::~DocumentHistoryDialog() {}

void DocumentHistoryDialog::loadHistory() {
    if (!m_db || !m_db->isOpen()) return;

    m_historyList->clear();

    m_entries = m_db->getDocumentHistory(m_documentId);

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

        auto docOpt = m_db->getDocument(m_documentId);
        QString title = docOpt ? docOpt->title : tr("Document");
        int folderId = docOpt ? docOpt->folderId : 0;

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

void DocumentHistoryDialog::onRestoreInPlace() {
    int idx = m_historyList->currentRow();
    if (idx < 0 || idx >= m_entries.size() || !m_db || !m_db->isOpen()) return;

    if (m_db->restoreDocumentHistoryVersion(m_documentId, m_entries[idx].id)) {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (widget->inherits("MainWindow")) {
                QMetaObject::invokeMethod(widget, "loadDocumentsAndNotes");
                break;
            }
        }
        accept();
    }
}
