#include "AIOperationsDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTextEdit>
#include <QVBoxLayout>

#include "AIOperationsManager.h"

AIOperationsDialog::AIOperationsDialog(BookDatabase* db, const QString& defaultPrompt, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("AI Document Operations"));
    resize(500, 400);

    QVBoxLayout* layout = new QVBoxLayout(this);

    // Operation
    QHBoxLayout* opLayout = new QHBoxLayout();
    opLayout->addWidget(new QLabel(tr("Operation:")));
    m_operationCombo = new QComboBox(this);

    QList<AIOperation> ops = AIOperationsManager::getMergedOperations(db);
    for (const AIOperation& op : ops) {
        m_operationCombo->addItem(op.name, op.id);
        m_operationCombo->setItemData(m_operationCombo->count() - 1, op.prompt, Qt::UserRole + 1);
    }

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
        QString prompt = m_operationCombo->itemData(index, Qt::UserRole + 1).toString();
        m_promptEdit->setPlainText(prompt);
    });

    // Set initial prompt if no default provided
    if (defaultPrompt.isEmpty() && m_operationCombo->count() > 0) {
        m_promptEdit->setPlainText(m_operationCombo->itemData(0, Qt::UserRole + 1).toString());
    }
}

AIOperationsDialog::~AIOperationsDialog() {}

QString AIOperationsDialog::getOperation() const { return m_operationCombo->currentData().toString(); }

QString AIOperationsDialog::getPrompt() const { return m_promptEdit->toPlainText(); }

void AIOperationsDialog::setForkOnlyMode(bool enabled) {
    if (enabled) {
        m_operationCombo->setEnabled(false);
    } else {
        m_operationCombo->setEnabled(true);
    }
}

void AIOperationsDialog::accept() {
    QString prompt = m_promptEdit->toPlainText();

    QRegularExpression inputRegex("\\{input\\s+\"([^\"]+)\"\\}");
    QRegularExpression textareaRegex("\\{textarea\\s+\"([^\"]+)\"\\}");

    bool hasInput = inputRegex.globalMatch(prompt).hasNext();
    bool hasTextarea = textareaRegex.globalMatch(prompt).hasNext();

    if (hasInput || hasTextarea) {
        QDialog formDialog(this);
        formDialog.setWindowTitle(tr("Provide Prompt Details"));
        QVBoxLayout* formLayout = new QVBoxLayout(&formDialog);

        QList<QPair<QString, QWidget*>> fields;

        QRegularExpressionMatchIterator i = inputRegex.globalMatch(prompt);
        while (i.hasNext()) {
            QRegularExpressionMatch match = i.next();
            QString labelText = match.captured(1);
            QLabel* label = new QLabel(labelText, &formDialog);
            QLineEdit* edit = new QLineEdit(&formDialog);
            formLayout->addWidget(label);
            formLayout->addWidget(edit);
            fields.append({match.captured(0), edit});
        }

        i = textareaRegex.globalMatch(prompt);
        while (i.hasNext()) {
            QRegularExpressionMatch match = i.next();
            QString labelText = match.captured(1);
            QLabel* label = new QLabel(labelText, &formDialog);
            QTextEdit* edit = new QTextEdit(&formDialog);
            formLayout->addWidget(label);
            formLayout->addWidget(edit);
            fields.append({match.captured(0), edit});
        }

        QDialogButtonBox* buttonBox =
            new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &formDialog);
        formLayout->addWidget(buttonBox);
        connect(buttonBox, &QDialogButtonBox::accepted, &formDialog, &QDialog::accept);
        connect(buttonBox, &QDialogButtonBox::rejected, &formDialog, &QDialog::reject);

        if (formDialog.exec() == QDialog::Accepted) {
            for (const auto& pair : fields) {
                QString val;
                if (QLineEdit* le = qobject_cast<QLineEdit*>(pair.second)) {
                    val = le->text();
                } else if (QTextEdit* te = qobject_cast<QTextEdit*>(pair.second)) {
                    val = te->toPlainText();
                }
                prompt.replace(pair.first, val);
            }
            m_promptEdit->setPlainText(prompt);
            QDialog::accept();
        }
    } else {
        QDialog::accept();
    }
}
