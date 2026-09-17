#ifndef DB_TRANSACTION_H
#define DB_TRANSACTION_H

#include "Database.h"

namespace db {

class Transaction {
   public:
    explicit Transaction(Database& db) : m_db(db), m_committed(false), m_failed(false) {
        if (!m_db.execute("BEGIN TRANSACTION;")) {
            m_failed = true;
        }
    }

    ~Transaction() {
        if (!m_committed && !m_failed) {
            rollback();
        }
    }

    bool commit() {
        if (m_failed) return false;
        if (m_db.execute("COMMIT;")) {
            m_committed = true;
            return true;
        }
        return false;
    }

    void rollback() {
        if (m_failed) return;
        m_db.execute("ROLLBACK;");
        m_failed = true;
    }

    bool isFailed() const { return m_failed; }

   private:
    Database& m_db;
    bool m_committed;
    bool m_failed;
};

}  // namespace db

#endif  // DB_TRANSACTION_H
