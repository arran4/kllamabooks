#include "DraftSelectionDialog.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTextBrowser>
#include <QTextEdit>
#include <QVBoxLayout>

DraftSelectionDialog::DraftSelectionDialog(std::shared_ptr<BookDatabase> db, int documentId,
                                           const QList<DocumentNode>& drafts, const QString& targetType, QWidget* parent)
    : QDialog(parent), m_db(db), m_documentId(documentId), m_drafts(drafts), m_targetType(targetType), m_draftSelected(false) {
    setWindowTitle(tr("Drafts Available"));
    resize(800, 600);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    QLabel* infoLabel = new QLabel(tr("There are drafts associated with this document. Select one to proceed:"));
    mainLayout->addWidget(infoLabel);

    m_mainSplitter = new QSplitter(Qt::Horizontal, this);

    QWidget* listWidgetContainer = new QWidget(this);
    QVBoxLayout* listLayout = new QVBoxLayout(listWidgetContainer);
    listLayout->setContentsMargins(0, 0, 0, 0);

    m_draftsList = new QListWidget(this);
    listLayout->addWidget(m_draftsList);
    m_mainSplitter->addWidget(listWidgetContainer);

    m_previewBrowser = new QTextBrowser(this);
    m_mainSplitter->addWidget(m_previewBrowser);

    m_mainSplitter->setSizes({250, 550});
    mainLayout->addWidget(m_mainSplitter, 1);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* useBtn = new QPushButton(tr("Use This"), this);
    QPushButton* delBtn = new QPushButton(tr("Delete View"), this);
    QPushButton* diffBtn = new QPushButton(tr("Show Differences"), this);
    QPushButton* cancelBtn = new QPushButton(tr("Ignore Drafts / Cancel"), this);

    btnLayout->addWidget(useBtn);
    btnLayout->addWidget(delBtn);
    btnLayout->addWidget(diffBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);

    mainLayout->addLayout(btnLayout);

    connect(m_draftsList, &QListWidget::itemSelectionChanged, this, &DraftSelectionDialog::onSelectionChanged);
    connect(useBtn, &QPushButton::clicked, this, &DraftSelectionDialog::onUseThis);
    connect(delBtn, &QPushButton::clicked, this, &DraftSelectionDialog::onDeleteView);
    connect(diffBtn, &QPushButton::clicked, this, &DraftSelectionDialog::onShowDifferences);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    refreshDraftsList();
}

DraftSelectionDialog::~DraftSelectionDialog() {}

QString DraftSelectionDialog::getSelectedDraftContent() const { return m_selectedContent; }

bool DraftSelectionDialog::didSelectDraft() const { return m_draftSelected; }

void DraftSelectionDialog::refreshDraftsList() {
    m_draftsList->clear();
    for (const auto& d : m_drafts) {
        QString displayTime = d.timestamp.toString("yyyy-MM-dd HH:mm:ss");
        QString title = d.title.isEmpty() ? tr("Untitled Draft") : d.title;
        m_draftsList->addItem(QString("%1\n%2").arg(title, displayTime));
    }
    if (m_draftsList->count() > 0) {
        m_draftsList->setCurrentRow(0);
    }
}

void DraftSelectionDialog::onSelectionChanged() {
    onPreview();
}

void DraftSelectionDialog::onUseThis() {
    int idx = m_draftsList->currentRow();
    if (idx >= 0 && idx < m_drafts.size()) {
        m_selectedContent = m_drafts[idx].content;
        m_draftSelected = true;
        accept();
    }
}

void DraftSelectionDialog::onDeleteView() {
    int idx = m_draftsList->currentRow();
    if (idx >= 0 && idx < m_drafts.size()) {
        int draftId = m_drafts[idx].id;
        if (m_db && m_db->isOpen()) {
            m_db->deleteDraft(draftId);
            m_drafts.removeAt(idx);
            refreshDraftsList();
        }
    }
}

void DraftSelectionDialog::onPreview() {
    int idx = m_draftsList->currentRow();
    if (idx >= 0 && idx < m_drafts.size()) {
        // Just show markdown in preview browser
        m_previewBrowser->setMarkdown(m_drafts[idx].content);

        // Remove 3-way view widgets if they exist
        while (m_mainSplitter->count() > 2) {
            QWidget* w = m_mainSplitter->widget(2);
            if (w) {
                w->hide();
                w->deleteLater();
            }
        }
        m_previewBrowser->show();
    } else {
        m_previewBrowser->clear();
    }
}

QString DraftSelectionDialog::getOriginalContent() const {
    if (!m_db || !m_db->isOpen()) return "";
    auto history = m_db->getDocumentHistory(m_documentId);
    if (!history.isEmpty()) {
        // Assume history is ordered by timestamp ASC, so the first is the oldest (original)
        return history.first().content;
    }
    return "";
}

QString DraftSelectionDialog::getCurrentDocumentContent() const {
    if (!m_db || !m_db->isOpen()) return "";

    QList<DocumentNode> docs;
    if (m_targetType == "template" || m_targetType == "templates") {
        docs = m_db->getTemplates(-1);
    } else if (m_targetType == "note" || m_targetType == "notes") {
        auto notes = m_db->getNotes(-1);
        for (const auto& n : notes) {
            if (n.id == m_documentId) return n.content;
        }
        return "";
    } else {
        docs = m_db->getDocuments(-1);
    }

    for (const auto& d : docs) {
        if (d.id == m_documentId) {
            return d.content;
        }
    }
    return "";
}

void DraftSelectionDialog::onShowDifferences() {
    int idx = m_draftsList->currentRow();
    if (idx < 0 || idx >= m_drafts.size()) return;

    QString originalContent = getOriginalContent();
    QString currentContent = getCurrentDocumentContent();
    QString draftContent = m_drafts[idx].content;

    // Hide the standard preview browser
    m_previewBrowser->hide();

    // Remove any existing 3-way view widgets
    while (m_mainSplitter->count() > 2) {
        QWidget* w = m_mainSplitter->widget(2);
        if (w) {
            w->hide();
            w->deleteLater();
        }
    }

    QWidget* threeWayWidget = new QWidget(m_mainSplitter);
    QHBoxLayout* threeWayLayout = new QHBoxLayout(threeWayWidget);
    threeWayLayout->setContentsMargins(0, 0, 0, 0);

    QSplitter* innerSplitter = new QSplitter(Qt::Horizontal, threeWayWidget);

    QWidget* col1 = new QWidget(innerSplitter);
    QVBoxLayout* lay1 = new QVBoxLayout(col1);
    lay1->setContentsMargins(0,0,0,0);
    lay1->addWidget(new QLabel(tr("Original (Forked From)")));
    QTextEdit* text1 = new QTextEdit(col1);
    text1->setPlainText(originalContent);
    text1->setReadOnly(true);
    lay1->addWidget(text1);

    QWidget* col2 = new QWidget(innerSplitter);
    QVBoxLayout* lay2 = new QVBoxLayout(col2);
    lay2->setContentsMargins(0,0,0,0);
    lay2->addWidget(new QLabel(tr("Current Document")));
    QTextEdit* text2 = new QTextEdit(col2);
    text2->setPlainText(currentContent);
    text2->setReadOnly(true);
    lay2->addWidget(text2);

    QWidget* col3 = new QWidget(innerSplitter);
    QVBoxLayout* lay3 = new QVBoxLayout(col3);
    lay3->setContentsMargins(0,0,0,0);
    lay3->addWidget(new QLabel(tr("Selected Draft")));
    QTextEdit* text3 = new QTextEdit(col3);
    text3->setPlainText(draftContent);
    text3->setReadOnly(true);
    lay3->addWidget(text3);

    innerSplitter->addWidget(col1);
    innerSplitter->addWidget(col2);
    innerSplitter->addWidget(col3);

    threeWayLayout->addWidget(innerSplitter);
    m_mainSplitter->addWidget(threeWayWidget);
    threeWayWidget->show();
}
