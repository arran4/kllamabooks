#include "DraftSelectionDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <algorithm>

DraftSelectionDialog::DraftSelectionDialog(std::shared_ptr<BookDatabase> db, const QString& originalContent, const QList<DocumentNode>& drafts, QWidget* parent)
    : QDialog(parent), m_db(db), m_originalContent(originalContent), m_drafts(drafts) {

    setWindowTitle(tr("Drafts Found"));
    resize(800, 500);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(new QLabel(tr("There are existing drafts for this document. What would you like to do?")));

    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);

    // Left pane: list of drafts
    QWidget* listWidget = new QWidget(splitter);
    QVBoxLayout* listLayout = new QVBoxLayout(listWidget);
    listLayout->setContentsMargins(0,0,0,0);
    m_draftList = new QListWidget(listWidget);
    for (const auto& draft : m_drafts) {
        QString itemText = QString("%1 (%2)").arg(draft.title).arg(draft.timestamp.toString("yyyy-MM-dd hh:mm:ss"));
        QListWidgetItem* item = new QListWidgetItem(itemText, m_draftList);
        item->setData(Qt::UserRole, draft.id);
    }
    listLayout->addWidget(m_draftList);

    // Right pane: preview
    QWidget* previewWidget = new QWidget(splitter);
    QVBoxLayout* previewLayout = new QVBoxLayout(previewWidget);
    previewLayout->setContentsMargins(0,0,0,0);
    m_previewEdit = new QTextEdit(previewWidget);
    m_previewEdit->setReadOnly(true);
    previewLayout->addWidget(new QLabel(tr("Preview:")));
    previewLayout->addWidget(m_previewEdit);

    splitter->addWidget(listWidget);
    splitter->addWidget(previewWidget);
    splitter->setSizes({250, 550});
    mainLayout->addWidget(splitter);

    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_diffBtn = new QPushButton(tr("Show Differences"), this);
    m_deleteBtn = new QPushButton(tr("Delete Draft"), this);
    m_useBtn = new QPushButton(tr("Use Draft"), this);
    QPushButton* editOriginalBtn = new QPushButton(tr("Edit Original"), this);
    QPushButton* cancelBtn = new QPushButton(tr("Cancel"), this);

    btnLayout->addWidget(m_diffBtn);
    btnLayout->addWidget(m_deleteBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(m_useBtn);
    btnLayout->addWidget(editOriginalBtn);
    btnLayout->addWidget(cancelBtn);

    mainLayout->addLayout(btnLayout);

    connect(m_draftList, &QListWidget::currentItemChanged, this, &DraftSelectionDialog::onDraftSelectionChanged);
    connect(m_useBtn, &QPushButton::clicked, this, &DraftSelectionDialog::onUseDraft);
    connect(m_deleteBtn, &QPushButton::clicked, this, &DraftSelectionDialog::onDeleteDraft);
    connect(editOriginalBtn, &QPushButton::clicked, this, &DraftSelectionDialog::onEditOriginal);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_diffBtn, &QPushButton::clicked, this, &DraftSelectionDialog::onShowDifferences);

    if (m_draftList->count() > 0) {
        m_draftList->setCurrentRow(0);
    } else {
        m_useBtn->setEnabled(false);
        m_deleteBtn->setEnabled(false);
        m_diffBtn->setEnabled(false);
    }
}

DraftSelectionDialog::Action DraftSelectionDialog::getSelectedAction() const {
    return m_selectedAction;
}

DocumentNode DraftSelectionDialog::getSelectedDraft() const {
    return m_selectedDraft;
}

void DraftSelectionDialog::onDraftSelectionChanged() {
    QListWidgetItem* item = m_draftList->currentItem();
    if (!item) {
        m_previewEdit->clear();
        m_useBtn->setEnabled(false);
        m_deleteBtn->setEnabled(false);
        m_diffBtn->setEnabled(false);
        return;
    }

    int draftId = item->data(Qt::UserRole).toInt();
    for (const auto& draft : m_drafts) {
        if (draft.id == draftId) {
            m_previewEdit->setPlainText(draft.content);
            m_useBtn->setEnabled(true);
            m_deleteBtn->setEnabled(true);
            m_diffBtn->setEnabled(true);
            break;
        }
    }
}

void DraftSelectionDialog::onUseDraft() {
    QListWidgetItem* item = m_draftList->currentItem();
    if (!item) return;

    int draftId = item->data(Qt::UserRole).toInt();
    for (const auto& draft : m_drafts) {
        if (draft.id == draftId) {
            m_selectedDraft = draft;
            m_selectedAction = UseDraft;
            accept();
            return;
        }
    }
}

void DraftSelectionDialog::onDeleteDraft() {
    QListWidgetItem* item = m_draftList->currentItem();
    if (!item) return;

    int draftId = item->data(Qt::UserRole).toInt();

    QMessageBox::StandardButton reply = QMessageBox::question(this, tr("Delete Draft"),
        tr("Are you sure you want to delete this draft?"),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        m_db->deleteDraft(draftId);
        int row = m_draftList->row(item);
        m_drafts.erase(std::remove_if(m_drafts.begin(), m_drafts.end(), [draftId](const DocumentNode& d) { return d.id == draftId; }), m_drafts.end());
        delete m_draftList->takeItem(row);

        if (m_draftList->count() == 0) {
            m_selectedAction = EditOriginal;
            accept();
        }
    }
}

void DraftSelectionDialog::onEditOriginal() {
    m_selectedAction = EditOriginal;
    accept();
}

void DraftSelectionDialog::onShowDifferences() {
    QListWidgetItem* item = m_draftList->currentItem();
    if (!item) return;

    int draftId = item->data(Qt::UserRole).toInt();
    QString draftContent;
    QString forkedContent;
    for (const auto& draft : m_drafts) {
        if (draft.id == draftId) {
            draftContent = draft.content;
            forkedContent = draft.forkedContent;
            break;
        }
    }

    QDialog diffDialog(this);
    diffDialog.setWindowTitle(tr("Compare Draft (3-Way View)"));
    diffDialog.resize(1200, 600);
    QVBoxLayout* mainLayout = new QVBoxLayout(&diffDialog);

    QSplitter* splitter = new QSplitter(Qt::Horizontal, &diffDialog);

    QWidget* forkedWidget = new QWidget(splitter);
    QVBoxLayout* forkedLayout = new QVBoxLayout(forkedWidget);
    forkedLayout->addWidget(new QLabel(tr("State when drafted:")));
    QTextEdit* forkedEdit = new QTextEdit(forkedWidget);
    forkedEdit->setReadOnly(true);
    forkedEdit->setPlainText(forkedContent);
    forkedLayout->addWidget(forkedEdit);

    QWidget* origWidget = new QWidget(splitter);
    QVBoxLayout* origLayout = new QVBoxLayout(origWidget);
    origLayout->addWidget(new QLabel(tr("Current Original Document:")));
    QTextEdit* origEdit = new QTextEdit(origWidget);
    origEdit->setReadOnly(true);
    origEdit->setPlainText(m_originalContent);
    origLayout->addWidget(origEdit);

    QWidget* draftWidget = new QWidget(splitter);
    QVBoxLayout* draftLayout = new QVBoxLayout(draftWidget);
    draftLayout->addWidget(new QLabel(tr("Draft Content:")));
    QTextEdit* draftEdit = new QTextEdit(draftWidget);
    draftEdit->setReadOnly(true);
    draftEdit->setPlainText(draftContent);
    draftLayout->addWidget(draftEdit);

    splitter->addWidget(forkedWidget);
    splitter->addWidget(origWidget);
    splitter->addWidget(draftWidget);

    mainLayout->addWidget(splitter);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    QPushButton* closeBtn = new QPushButton(tr("Close"), &diffDialog);
    btnLayout->addWidget(closeBtn);
    mainLayout->addLayout(btnLayout);

    connect(closeBtn, &QPushButton::clicked, &diffDialog, &QDialog::accept);

    diffDialog.exec();
}
