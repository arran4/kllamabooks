#ifndef DATABASESETTINGSDIALOG_H
#define DATABASESETTINGSDIALOG_H

#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QTextEdit>

#include "BookDatabase.h"

class DatabaseSettingsDialog : public QDialog {
    Q_OBJECT
   public:
    explicit DatabaseSettingsDialog(BookDatabase* db, const QStringList& availableModels, QWidget* parent = nullptr);
    ~DatabaseSettingsDialog() = default;

   private slots:
    void saveSettings();

   private:
    BookDatabase* m_db;

    QComboBox* m_defaultModelCombo;
    QLineEdit* m_userNameEdit;
    QLineEdit* m_assistantNameEdit;
    QTextEdit* m_systemPromptEdit;
};

#endif  // DATABASESETTINGSDIALOG_H
