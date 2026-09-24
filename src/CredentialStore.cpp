#include "CredentialStore.h"

#include <KF6/KWallet/KWallet>

namespace {
const QString FOLDER_NAME = "kllamabooks_llm_credentials";

class DefaultWallet : public IWallet {
   public:
    explicit DefaultWallet(KWallet::Wallet* w) : m_wallet(w) {}
    ~DefaultWallet() override { delete m_wallet; }

    DefaultWallet(const DefaultWallet&) = delete;
    DefaultWallet& operator=(const DefaultWallet&) = delete;

    bool hasFolder(const QString& f) override { return m_wallet->hasFolder(f); }
    bool createFolder(const QString& f) override { return m_wallet->createFolder(f); }
    bool setFolder(const QString& f) override { return m_wallet->setFolder(f); }
    bool hasEntry(const QString& key) override { return m_wallet->hasEntry(key); }
    int writePassword(const QString& key, const QString& value) override { return m_wallet->writePassword(key, value); }
    int readPassword(const QString& key, QString& value) override { return m_wallet->readPassword(key, value); }
    int removeEntry(const QString& key) override { return m_wallet->removeEntry(key); }

   private:
    KWallet::Wallet* m_wallet;
};

class DefaultWalletProvider : public IWalletProvider {
   public:
    IWallet* openWallet() override {
        KWallet::Wallet* w =
            KWallet::Wallet::openWallet(KWallet::Wallet::LocalWallet(), 0, KWallet::Wallet::Synchronous);
        if (!w) return nullptr;
        return new DefaultWallet(w);
    }
};

}  // namespace

KWalletCredentialStore::KWalletCredentialStore(IWalletProvider* provider) : m_provider(provider) {
    if (!m_provider) {
        m_provider = new DefaultWalletProvider();
    }
}

KWalletCredentialStore::~KWalletCredentialStore() { delete m_provider; }

CredentialStore::Result KWalletCredentialStore::writeCredential(const QString& id, const QString& secret) {
    IWallet* wallet = m_provider->openWallet();
    if (!wallet) {
        return Result::WalletUnavailable;
    }

    if (!wallet->hasFolder(FOLDER_NAME)) {
        if (!wallet->createFolder(FOLDER_NAME)) {
            delete wallet;
            return Result::WalletUnavailable;
        }
    }
    if (!wallet->setFolder(FOLDER_NAME)) {
        delete wallet;
        return Result::WalletUnavailable;
    }

    int ret = wallet->writePassword(id, secret);
    delete wallet;

    return (ret == 0) ? Result::Success : Result::WriteFailure;
}

CredentialStore::Result KWalletCredentialStore::readCredential(const QString& id, QString& secret) {
    IWallet* wallet = m_provider->openWallet();
    if (!wallet) {
        return Result::WalletUnavailable;
    }

    if (!wallet->hasFolder(FOLDER_NAME)) {
        delete wallet;
        return Result::NotFound;
    }
    if (!wallet->setFolder(FOLDER_NAME)) {
        delete wallet;
        return Result::WalletUnavailable;
    }

    if (!wallet->hasEntry(id)) {
        delete wallet;
        return Result::NotFound;
    }

    int ret = wallet->readPassword(id, secret);
    delete wallet;

    return (ret == 0) ? Result::Success : Result::ReadFailure;
}

CredentialStore::Result KWalletCredentialStore::deleteCredential(const QString& id) {
    IWallet* wallet = m_provider->openWallet();
    if (!wallet) {
        return Result::WalletUnavailable;
    }

    if (!wallet->hasFolder(FOLDER_NAME)) {
        delete wallet;
        return Result::Success;  // Already gone
    }
    if (!wallet->setFolder(FOLDER_NAME)) {
        delete wallet;
        return Result::WalletUnavailable;
    }

    if (!wallet->hasEntry(id)) {
        delete wallet;
        return Result::Success;  // Already gone
    }

    int ret = wallet->removeEntry(id);
    delete wallet;

    return (ret == 0) ? Result::Success : Result::DeleteFailure;
}
