#ifndef DOCUMENTEDITWINDOW_H
#define DOCUMENTEDITWINDOW_H

#include <QWidget>
#include <QString>
#include <QDateTime>
#include <memory>
#include "BookDatabase.h"

class QTextEdit;
class QPushButton;

class DocumentEditWindow : public QWidget {
    Q_OBJECT
public:
    explicit DocumentEditWindow(std::shared_ptr<BookDatabase> db, int documentId, const QString& title, QWidget* parent = nullptr);
    ~DocumentEditWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onSaveClicked();
    void onCancelClicked();

private:
    std::shared_ptr<BookDatabase> m_db;
    int m_documentId;
    QString m_title;
    QDateTime m_openTimestamp;
    QString m_initialContent;

    QTextEdit* m_editor;
    QPushButton* m_saveBtn;
    QPushButton* m_cancelBtn;

    QDateTime getLatestDbTimestamp() const;
    void loadDocument();
    bool saveToDb();
    int forkDocument();
    int saveToDraft();
};

#endif // DOCUMENTEDITWINDOW_H
