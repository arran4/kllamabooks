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

class IWallet {
   public:
    virtual ~IWallet() = default;
    virtual bool hasFolder(const QString& f) = 0;
    virtual bool createFolder(const QString& f) = 0;
    virtual bool setFolder(const QString& f) = 0;
    virtual bool hasEntry(const QString& key) = 0;
    virtual int writePassword(const QString& key, const QString& value) = 0;
    virtual int readPassword(const QString& key, QString& value) = 0;
    virtual int removeEntry(const QString& key) = 0;
};

class IWalletProvider {
   public:
    virtual ~IWalletProvider() = default;
    virtual IWallet* openWallet() = 0;
};

class KWalletCredentialStore : public CredentialStore {
   public:
    explicit KWalletCredentialStore(IWalletProvider* provider = nullptr);
    ~KWalletCredentialStore() override;

    Result writeCredential(const QString& id, const QString& secret) override;
    Result readCredential(const QString& id, QString& secret) override;
    Result deleteCredential(const QString& id) override;

   private:
    IWalletProvider* m_provider;
};

#endif  // CREDENTIALSTORE_H
