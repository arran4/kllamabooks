#include "NewDocumentDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "BookDatabase.h"
#include "DocumentTemplatesManager.h"

NewDocumentDialog::NewDocumentDialog(std::shared_ptr<BookDatabase> db, int defaultFolderId, QWidget* parent)
    : QDialog(parent), m_db(db), m_defaultFolderId(defaultFolderId) {
    setWindowTitle(tr("New Document"));
    resize(450, 550);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Document Type
    mainLayout->addWidget(new QLabel(tr("Document Type:"), this));
    m_typeCombo = new QComboBox(this);
    m_typeCombo->addItem(tr("Standard Document"), FromTemplate);
    m_typeCombo->addItem(tr("From AI Prompt"), FromPrompt);
    m_typeCombo->addItem(tr("Resume a Draft"), ResumeDraft);
    mainLayout->addWidget(m_typeCombo);

    // Title
    mainLayout->addWidget(new QLabel(tr("Title:"), this));
    m_titleEdit = new QLineEdit(this);
    mainLayout->addWidget(m_titleEdit);

    // Prompt Widget (Hidden by default)
    m_promptWidget = new QWidget(this);
    QVBoxLayout* promptLayout = new QVBoxLayout(m_promptWidget);
    promptLayout->setContentsMargins(0, 0, 0, 0);
    promptLayout->addWidget(new QLabel(tr("AI Prompt:"), m_promptWidget));
    m_promptEdit = new QTextEdit(m_promptWidget);
    promptLayout->addWidget(m_promptEdit);
    mainLayout->addWidget(m_promptWidget);

    // Template Widget
    m_templateWidget = new QWidget(this);
    QVBoxLayout* templateLayout = new QVBoxLayout(m_templateWidget);
    templateLayout->setContentsMargins(0, 0, 0, 0);
    templateLayout->addWidget(new QLabel(tr("Select Template:"), m_templateWidget));
    m_templateCombo = new QComboBox(m_templateWidget);
    templateLayout->addWidget(m_templateCombo);
    mainLayout->addWidget(m_templateWidget);

    // Draft Widget (Hidden by default)
    m_draftWidget = new QWidget(this);
    QVBoxLayout* draftLayout = new QVBoxLayout(m_draftWidget);
    draftLayout->setContentsMargins(0, 0, 0, 0);
    draftLayout->addWidget(new QLabel(tr("Select Draft:"), m_draftWidget));
    m_draftCombo = new QComboBox(m_draftWidget);
    draftLayout->addWidget(m_draftCombo);

    m_overwriteCheck = new QCheckBox(tr("Overwrite Existing Document"), m_draftWidget);
    draftLayout->addWidget(m_overwriteCheck);

    m_documentCombo = new QComboBox(m_draftWidget);
    m_documentCombo->setEnabled(false);
    draftLayout->addWidget(m_documentCombo);

    connect(m_overwriteCheck, &QCheckBox::checkStateChanged, this, &NewDocumentDialog::onOverwriteToggled);

    mainLayout->addWidget(m_draftWidget);

    // Folder Selection
    mainLayout->addWidget(new QLabel(tr("Location:"), this));
    m_folderTree = new QTreeWidget(this);
    m_folderTree->setHeaderHidden(true);

    QTreeWidgetItem* rootItem = new QTreeWidgetItem(m_folderTree);
    rootItem->setText(0, tr("Root (Documents)"));
    rootItem->setData(0, Qt::UserRole, 0);

    if (m_defaultFolderId == 0) {
        rootItem->setSelected(true);
    }

    if (m_db) {
        populateFolders(rootItem, 0);
        populateTemplates();
        populateDrafts();
        populateDocuments();
    }

    rootItem->setExpanded(true);
    mainLayout->addWidget(m_folderTree);

    // Buttons
    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NewDocumentDialog::onTypeChanged);

    onTypeChanged(m_typeCombo->currentIndex());
    // We intentionally do not connect itemExpanded / itemCollapsed to save the state,
    // so that the user's opens and closes in this dialog do not impact the original VFS.
}

void NewDocumentDialog::populateFolders(QTreeWidgetItem* parentItem, int parentId) {
    if (!m_db) return;
    QList<FolderNode> folders = m_db->getFolders("documents");
    for (const auto& folder : folders) {
        if (folder.parentId == parentId) {
            QTreeWidgetItem* item = new QTreeWidgetItem(parentItem);
            item->setText(0, folder.name);
            item->setData(0, Qt::UserRole, folder.id);
            if (folder.id == m_defaultFolderId) {
                item->setSelected(true);
            }
            populateFolders(item, folder.id);
            if (folder.isExpanded) {
                item->setExpanded(true);
            }
        }
    }
}

void NewDocumentDialog::populateTemplates() {
    QList<DocumentTemplate> templates = DocumentTemplatesManager::getMergedTemplates(m_db.get());
    for (const auto& tpl : templates) {
        m_templateCombo->addItem(tpl.name, tpl.id);
    }
    if (m_templateCombo->count() == 0) {
        m_templateCombo->addItem(tr("No templates available"), "");
        m_templateCombo->setEnabled(false);
    }
}

void NewDocumentDialog::populateDrafts() {
    if (!m_db) return;
    QList<DocumentNode> drafts = m_db->getDrafts(-1);  // Get all drafts
    for (const auto& draft : drafts) {
        m_draftCombo->addItem(draft.title, draft.id);
    }
    if (m_draftCombo->count() == 0) {
        m_draftCombo->addItem(tr("No drafts available"), -1);
        m_draftCombo->setEnabled(false);
    }
}

void NewDocumentDialog::populateDocuments() {
    if (!m_db) return;
    QList<DocumentNode> docs = m_db->getDocuments(-1);  // Get all documents
    for (const auto& doc : docs) {
        m_documentCombo->addItem(doc.title, doc.id);
    }
    if (m_documentCombo->count() == 0) {
        m_documentCombo->addItem(tr("No existing documents"), -1);
        m_overwriteCheck->setEnabled(false);
    }
}

void NewDocumentDialog::onOverwriteToggled(int state) {
    m_documentCombo->setEnabled(state == Qt::Checked);
    m_titleEdit->setEnabled(state != Qt::Checked);
}

void NewDocumentDialog::onTypeChanged(int index) {
    DocumentType type = static_cast<DocumentType>(m_typeCombo->itemData(index).toInt());

    m_promptWidget->setVisible(type == FromPrompt);
    m_templateWidget->setVisible(type == FromTemplate);
    m_draftWidget->setVisible(type == ResumeDraft);

    switch (type) {
        case FromPrompt:
            m_titleEdit->setPlaceholderText(tr("AI Generated Document"));
            break;
        case FromTemplate:
            m_titleEdit->setPlaceholderText(tr("New Document"));
            break;
        case ResumeDraft:
            m_titleEdit->setPlaceholderText(tr("Document from Draft"));
            break;
    }
}

NewDocumentDialog::DocumentType NewDocumentDialog::getDocumentType() const {
    return static_cast<DocumentType>(m_typeCombo->currentData().toInt());
}

QString NewDocumentDialog::getTitle() const {
    QString text = m_titleEdit->text().trimmed();
    return text.isEmpty() ? m_titleEdit->placeholderText() : text;
}

int NewDocumentDialog::getSelectedFolderId() const {
    QList<QTreeWidgetItem*> selected = m_folderTree->selectedItems();
    if (!selected.isEmpty()) {
        return selected.first()->data(0, Qt::UserRole).toInt();
    }
    return 0;  // Root
}

QString NewDocumentDialog::getPrompt() const { return m_promptEdit->toPlainText(); }

QString NewDocumentDialog::getSelectedTemplateId() const { return m_templateCombo->currentData().toString(); }

int NewDocumentDialog::getSelectedDraftId() const { return m_draftCombo->currentData().toInt(); }

bool NewDocumentDialog::isOverwriteDocument() const { return m_overwriteCheck->isChecked(); }

int NewDocumentDialog::getOverwriteDocumentId() const { return m_documentCombo->currentData().toInt(); }
