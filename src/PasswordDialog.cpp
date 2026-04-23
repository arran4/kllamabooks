#include "PasswordDialog.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QDialogButtonBox>

PasswordDialog::PasswordDialog(const QString& title, const QString& labelText, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(title);

    QVBoxLayout* layout = new QVBoxLayout(this);

    QLabel* label = new QLabel(labelText, this);
    layout->addWidget(label);

    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    layout->addWidget(m_passwordEdit);

    m_saveWalletCheckBox = new QCheckBox(tr("Save this password to kwallet"), this);
    m_saveWalletCheckBox->setChecked(true); // Default to checked
    layout->addWidget(m_saveWalletCheckBox);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);
}

QString PasswordDialog::password() const {
    return m_passwordEdit->text();
}

bool PasswordDialog::saveToWallet() const {
    return m_saveWalletCheckBox->isChecked();
}
