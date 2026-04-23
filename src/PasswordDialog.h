#pragma once

#include <QDialog>
#include <QString>

class QLineEdit;
class QCheckBox;

class PasswordDialog : public QDialog {
    Q_OBJECT

public:
    explicit PasswordDialog(const QString& title, const QString& labelText, QWidget* parent = nullptr);

    QString password() const;
    bool saveToWallet() const;

private:
    QLineEdit* m_passwordEdit;
    QCheckBox* m_saveWalletCheckBox;
};
