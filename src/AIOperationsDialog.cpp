#include "AIOperationsDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QTextEdit>
#include <QPushButton>
#include <QSettings>

AIOperationsDialog::AIOperationsDialog(const QString& defaultPrompt, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("AI Document Operations"));
    resize(500, 400);

    QVBoxLayout* layout = new QVBoxLayout(this);

    // Operation
    QHBoxLayout* opLayout = new QHBoxLayout();
    opLayout->addWidget(new QLabel(tr("Operation:")));
    m_operationCombo = new QComboBox(this);
    m_operationCombo->addItem(tr("Complete this text"), "complete");
    m_operationCombo->addItem(tr("Replace entirely"), "replace");
    m_operationCombo->addItem(tr("Replace in place"), "replace_in_place");
    opLayout->addWidget(m_operationCombo, 1);
    layout->addLayout(opLayout);

    // Prompt
    layout->addWidget(new QLabel(tr("Prompt Instructions:")));
    m_promptEdit = new QTextEdit(this);
    m_promptEdit->setPlainText(defaultPrompt);
    layout->addWidget(m_promptEdit);

    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* cancelBtn = new QPushButton(tr("Cancel"), this);
    QPushButton* generateBtn = new QPushButton(tr("Generate"), this);
    generateBtn->setDefault(true);

    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(generateBtn);
    layout->addLayout(btnLayout);

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(generateBtn, &QPushButton::clicked, this, &QDialog::accept);

    // Auto-update prompt when operation changes
    connect(m_operationCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        QString op = m_operationCombo->itemData(index).toString();
        QSettings settings;
        if (op == "complete") {
            m_promptEdit->setPlainText(settings.value("prompt_complete", "Complete the following text naturally. Only output the continuation.\n\nText:\n{context}").toString());
        } else if (op == "replace") {
            m_promptEdit->setPlainText(settings.value("prompt_replace_entirely", "Rewrite the following document according to your instructions. Only output the rewritten document, nothing else.\n\nInstructions: <your instructions here>\n\nDocument:\n{context}").toString());
        } else if (op == "replace_in_place") {
            m_promptEdit->setPlainText(settings.value("prompt_replace_in_place", "Rewrite the following text according to your instructions. Only output the rewritten text, nothing else.\n\nInstructions: <your instructions here>\n\nText:\n{context}").toString());
        }
    });
}

AIOperationsDialog::~AIOperationsDialog() {}

QString AIOperationsDialog::getOperation() const {
    return m_operationCombo->currentData().toString();
}

QString AIOperationsDialog::getPrompt() const {
    return m_promptEdit->toPlainText();
}
