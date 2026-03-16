#include "ModelSelectionDialog.h"

#include <QIcon>
#include <QSettings>
#include <QVBoxLayout>

ModelSelectionDialog::ModelSelectionDialog(const QStringList& models, QWidget* parent)
    : QDialog(parent), m_allModels(models) {
    setWindowTitle("Select Model");
    resize(400, 300);
    setupUi();
}

ModelSelectionDialog::~ModelSelectionDialog() {}

QString ModelSelectionDialog::selectedModel() const { return m_selectedModel; }

void ModelSelectionDialog::setupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    m_searchField = new QLineEdit(this);
    m_searchField->setPlaceholderText("Search models...");
    m_searchField->setClearButtonEnabled(true);
    mainLayout->addWidget(m_searchField);

    m_modelList = new QListWidget(this);
    mainLayout->addWidget(m_modelList);

    QSettings settings;
    QStringList favorites = settings.value("favoriteModels").toStringList();

    // Populate the list
    for (const QString& model : m_allModels) {
        if (favorites.contains(model)) {
            QListWidgetItem* item = new QListWidgetItem("⭐ " + model);
            item->setData(Qt::UserRole, model);
            m_modelList->addItem(item);
        }
    }

    for (const QString& model : m_allModels) {
        if (!favorites.contains(model)) {
            QListWidgetItem* item = new QListWidgetItem(model);
            item->setData(Qt::UserRole, model);
            m_modelList->addItem(item);
        }
    }

    // Connect signals
    connect(m_searchField, &QLineEdit::textChanged, this, &ModelSelectionDialog::onSearchChanged);
    connect(m_modelList, &QListWidget::itemDoubleClicked, this, &ModelSelectionDialog::onItemSelected);
    // Allow enter to select if exactly one is visible
    connect(m_searchField, &QLineEdit::returnPressed, this, [this]() {
        QListWidgetItem* selected = nullptr;
        int visibleCount = 0;
        for (int i = 0; i < m_modelList->count(); ++i) {
            QListWidgetItem* item = m_modelList->item(i);
            if (!item->isHidden()) {
                visibleCount++;
                selected = item;
            }
        }
        if (visibleCount == 1 && selected) {
            onItemSelected(selected);
        }
    });
}

void ModelSelectionDialog::onSearchChanged(const QString& text) {
    QString query = text.toLower();
    for (int i = 0; i < m_modelList->count(); ++i) {
        QListWidgetItem* item = m_modelList->item(i);
        QString itemName = item->data(Qt::UserRole).toString();
        item->setHidden(!itemName.toLower().contains(query));
    }
}

void ModelSelectionDialog::onItemSelected(QListWidgetItem* item) {
    if (item) {
        m_selectedModel = item->data(Qt::UserRole).toString();
        accept();
    }
}
