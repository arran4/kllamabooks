#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QMessageBox>

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog();

signals:
    void settingsApplied();

private slots:
    void onApply();
    void onTestConnection();

private:
    QLineEdit* m_urlEdit;
    QPushButton* m_testButton;
    QPushButton* m_applyButton;
    QPushButton* m_cancelButton;
    QSettings m_settings;
};

#endif // SETTINGSDIALOG_H
