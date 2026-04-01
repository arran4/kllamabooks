#include "AIOperationsDialog.h"
#include "AIOperationsManager.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QRegularExpression>
#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>

AIOperationsDialog::AIOperationsDialog(BookDatabase* db, const QString& defaultPrompt, QWidget* parent) : QDialog(parent) {
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

QString AIOperationsDialog::getPrompt() const { return m_finalPrompt; }

void AIOperationsDialog::setForkOnlyMode(bool enabled) {
    if (enabled) {
        m_operationCombo->setEnabled(false);
    } else {
        m_operationCombo->setEnabled(true);
    }
}

void AIOperationsDialog::accept() {
    QString prompt = m_promptEdit->toPlainText();
    m_finalPrompt = prompt;

    QRegularExpression re("\\{(input|textarea)\\s+\"([^\"]+)\"\\}");
    QRegularExpressionMatchIterator i = re.globalMatch(prompt);

    if (i.hasNext()) {
        QDialog inputDlg(this);
        inputDlg.setWindowTitle(tr("Provide Inputs"));
        inputDlg.resize(400, 300);
        QVBoxLayout* layout = new QVBoxLayout(&inputDlg);
        QFormLayout* form = new QFormLayout();
        layout->addLayout(form);

        QList<QPair<QString, QWidget*>> inputWidgets;

        while (i.hasNext()) {
            QRegularExpressionMatch match = i.next();
            QString type = match.captured(1);
            QString label = match.captured(2);
            QString fullMatch = match.captured(0);

            QWidget* w = nullptr;
            if (type == "input") {
                w = new QLineEdit(&inputDlg);
            } else if (type == "textarea") {
                w = new QTextEdit(&inputDlg);
            }

            if (w) {
                form->addRow(label + ":", w);
                inputWidgets.append({fullMatch, w});
            }
        }

        QHBoxLayout* btnLayout = new QHBoxLayout();
        QPushButton* cancelBtn = new QPushButton(tr("Cancel"), &inputDlg);
        QPushButton* okBtn = new QPushButton(tr("OK"), &inputDlg);
        btnLayout->addStretch();
        btnLayout->addWidget(cancelBtn);
        btnLayout->addWidget(okBtn);
        layout->addLayout(btnLayout);

        connect(cancelBtn, &QPushButton::clicked, &inputDlg, &QDialog::reject);
        connect(okBtn, &QPushButton::clicked, &inputDlg, &QDialog::accept);

        if (inputDlg.exec() == QDialog::Accepted) {
            for (const auto& pair : inputWidgets) {
                QString val;
                if (QLineEdit* le = qobject_cast<QLineEdit*>(pair.second)) {
                    val = le->text();
                } else if (QTextEdit* te = qobject_cast<QTextEdit*>(pair.second)) {
                    val = te->toPlainText();
                }
                prompt.replace(pair.first, val);
            }
            m_finalPrompt = prompt;
            QDialog::accept();
        }
    } else {
        QDialog::accept();
    }
}
