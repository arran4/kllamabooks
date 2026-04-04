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

struct InputWidgetInfo {
    int start;
    int length;
    QWidget* widget;
};

void AIOperationsDialog::accept() {
    QString prompt = m_promptEdit->toPlainText();
    m_finalPrompt = prompt;

    QRegularExpression re("\\{(input|textarea)(?:\\s+\"([^\"]+)\")?\\}");
    QRegularExpressionMatchIterator i = re.globalMatch(prompt);

    if (i.hasNext()) {
        QDialog inputDlg(this);
        inputDlg.setWindowTitle(tr("Provide Inputs"));
        inputDlg.resize(400, 300);
        QVBoxLayout* layout = new QVBoxLayout(&inputDlg);
        QFormLayout* form = new QFormLayout();
        layout->addLayout(form);

        QList<InputWidgetInfo> inputWidgets;

        while (i.hasNext()) {
            QRegularExpressionMatch match = i.next();
            QString type = match.captured(1);
            QString label = match.captured(2);

            if (label.isEmpty()) {
                int start = match.capturedStart(0);
                int lineStart = prompt.lastIndexOf('\n', start - 1) + 1;
                QString preceding = prompt.mid(lineStart, start - lineStart).trimmed();
                if (!preceding.isEmpty()) {
                    label = preceding;
                } else {
                    int end = match.capturedEnd(0);
                    int nextLineStart = prompt.indexOf('\n', end);
                    if (nextLineStart != -1) {
                        int nextLineEnd = prompt.indexOf('\n', nextLineStart + 1);
                        if (nextLineEnd == -1) nextLineEnd = prompt.length();
                        QString nextLine = prompt.mid(nextLineStart + 1, nextLineEnd - nextLineStart - 1).trimmed();
                        if (!nextLine.isEmpty() && nextLine.length() < 100) {
                            label = nextLine;
                        }
                    }
                }
                if (label.isEmpty()) {
                    label = "Input";
                }
            }

            QWidget* w = nullptr;
            if (type == "input") {
                w = new QLineEdit(&inputDlg);
            } else if (type == "textarea") {
                w = new QTextEdit(&inputDlg);
            }

            if (w) {
                form->addRow(label + ":", w);
                inputWidgets.append({match.capturedStart(0), match.capturedLength(0), w});
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
            // Replace backwards to preserve indices
            for (int j = inputWidgets.size() - 1; j >= 0; --j) {
                const auto& info = inputWidgets[j];
                QString val;
                if (QLineEdit* le = qobject_cast<QLineEdit*>(info.widget)) {
                    val = le->text();
                } else if (QTextEdit* te = qobject_cast<QTextEdit*>(info.widget)) {
                    val = te->toPlainText();
                }
                prompt.replace(info.start, info.length, val);
            }
            m_finalPrompt = prompt;
            QDialog::accept();
        }
    } else {
        QDialog::accept();
    }
}
