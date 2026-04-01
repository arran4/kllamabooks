#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QComboBox>
#include <QDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QVariantList>
#include <QVariantMap>

#include <QSpinBox>

class ConnectionDialog : public QDialog {
    Q_OBJECT
   public:
    explicit ConnectionDialog(QWidget* parent = nullptr, const QString& name = "New Connection",
                              const QString& backend = "Ollama", const QString& url = "http://localhost:11434",
                              const QString& authKey = "", int concurrency = 1);
    ~ConnectionDialog();

    QString name() const;
    QString backend() const;
    QString url() const;
    QString authKey() const;
    int concurrency() const;

   private slots:
    void onTestConnection();

   private:
    QLineEdit* m_nameEdit;
    QComboBox* m_backendCombo;
    QLineEdit* m_urlEdit;
    QLineEdit* m_authKeyEdit;
    QSpinBox* m_concurrencySpin;
    QPushButton* m_testButton;
};

class SettingsDialog : public QDialog {
    Q_OBJECT
   public:
    explicit SettingsDialog(QWidget* parent = nullptr);
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
    QComboBox* m_sendBehaviorCombo;
    QTextEdit* m_globalSystemPromptEdit;
    QComboBox* m_queueProcessingCombo;
    QCheckBox* m_prioritizeSameModelCheck;
};

#endif  // SETTINGSDIALOG_H
