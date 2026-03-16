#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QTableWidget>
#include <QHeaderView>
#include <QVariantMap>
#include <QVariantList>
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
    void onAddConnection();
    void onRemoveConnection();
    void onEditConnection();
    void loadConnections();
    void saveConnections();
    void onTestConnection();

private:
    QTableWidget* m_connectionsTable;
    QPushButton* m_addButton;
    QPushButton* m_removeButton;
    QPushButton* m_editButton;
    QPushButton* m_testButton;
    QPushButton* m_applyButton;
    QPushButton* m_cancelButton;
    QSettings m_settings;
};

#endif // SETTINGSDIALOG_H
