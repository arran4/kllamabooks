#ifndef FAKECREDENTIALSTORE_H
#define FAKECREDENTIALSTORE_H

#include <QHash>

#include "../src/CredentialStore.h"

class FakeCredentialStore : public CredentialStore {
   public:
    Result writeCredential(const QString& id, const QString& secret) override {
        if (simulateUnavailable) return Result::WalletUnavailable;
        if (simulateWriteFailure || failWriteIds.contains(id)) return Result::WriteFailure;

        m_store[id] = secret;
        return Result::Success;
    }

    Result readCredential(const QString& id, QString& secret) override {
        if (simulateUnavailable) return Result::WalletUnavailable;
        if (simulateReadFailure) return Result::ReadFailure;

        if (!m_store.contains(id)) {
            return Result::NotFound;
        }

        secret = m_store[id];
        return Result::Success;
    }

    Result deleteCredential(const QString& id) override {
        if (simulateUnavailable) return Result::WalletUnavailable;
        if (simulateDeleteFailure) return Result::DeleteFailure;

        m_store.remove(id);
        return Result::Success;
    }

    // For test verification
    bool hasCredential(const QString& id) const { return m_store.contains(id); }

    QString getCredential(const QString& id) const { return m_store.value(id); }

    void clear() {
        m_store.clear();
        simulateUnavailable = false;
        simulateWriteFailure = false;
        simulateReadFailure = false;
        simulateDeleteFailure = false;
    }

    // Toggles for simulating errors
    bool simulateUnavailable = false;
    bool simulateWriteFailure = false;
    QSet<QString> failWriteIds;
    bool simulateReadFailure = false;
    bool simulateDeleteFailure = false;

   private:
    QHash<QString, QString> m_store;
};

#endif  // FAKECREDENTIALSTORE_H
