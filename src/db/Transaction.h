#ifndef DB_TRANSACTION_H
#define DB_TRANSACTION_H

#include <atomic>

#include "Database.h"

namespace db {

class Transaction {
   public:
    explicit Transaction(Database& db) : m_db(db), m_committed(false), m_failed(false), m_isSavepoint(false) {
        if (m_db.inTransaction()) {
            m_isSavepoint = true;
            static std::atomic<quint64> counter{0};
            m_savepointName = QString("sp_%1").arg(++counter);
            if (!m_db.execute(QString("SAVEPOINT %1;").arg(m_savepointName))) {
                m_failed = true;
            }
        } else {
            if (!m_db.execute("BEGIN TRANSACTION;")) {
                m_failed = true;
            }
        }
    }

    ~Transaction() {
        if (!m_committed && !m_failed) {
            rollback();
        }
    }

    bool commit() {
        if (m_failed) return false;
        const bool ok = m_isSavepoint ? m_db.execute(QString("RELEASE SAVEPOINT %1;").arg(m_savepointName))
                                      : m_db.execute("COMMIT;");
        if (ok) {
            m_committed = true;
            return true;
        }
        return false;
    }

    void rollback() {
        if (m_failed) return;
        if (m_isSavepoint) {
            m_db.execute(QString("ROLLBACK TO SAVEPOINT %1;").arg(m_savepointName));
            m_db.execute(QString("RELEASE SAVEPOINT %1;").arg(m_savepointName));
        } else {
            m_db.execute("ROLLBACK;");
        }
        m_failed = true;
    }

    bool isFailed() const { return m_failed; }

   private:
    Database& m_db;
    bool m_committed;
    bool m_failed;
    bool m_isSavepoint;
    QString m_savepointName;
};

}  // namespace db

#endif  // DB_TRANSACTION_H
