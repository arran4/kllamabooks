#ifndef CREDENTIALSTORE_H
#define CREDENTIALSTORE_H

#include <QString>

class CredentialStore {
   public:
    enum class Result { Success, NotFound, WalletUnavailable, ReadFailure, WriteFailure, DeleteFailure };

    virtual ~CredentialStore() = default;

    virtual Result writeCredential(const QString& id, const QString& secret) = 0;
    virtual Result readCredential(const QString& id, QString& secret) = 0;
    virtual Result deleteCredential(const QString& id) = 0;
};

class KWalletCredentialStore : public CredentialStore {
   public:
    KWalletCredentialStore();
    ~KWalletCredentialStore() override;

    Result writeCredential(const QString& id, const QString& secret) override;
    Result readCredential(const QString& id, QString& secret) override;
    Result deleteCredential(const QString& id) override;
};

#endif  // CREDENTIALSTORE_H
