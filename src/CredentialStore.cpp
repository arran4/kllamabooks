#include "CredentialStore.h"
#include <KF6/KWallet/KWallet>

namespace {
const QString FOLDER_NAME = "kllamabooks_llm_credentials";
}

KWalletCredentialStore::KWalletCredentialStore() {}

KWalletCredentialStore::~KWalletCredentialStore() {}

CredentialStore::Result KWalletCredentialStore::writeCredential(const QString& id, const QString& secret) {
    KWallet::Wallet* wallet =
        KWallet::Wallet::openWallet(KWallet::Wallet::LocalWallet(), 0, KWallet::Wallet::Synchronous);
    if (!wallet) {
        return Result::WalletUnavailable;
    }

    if (!wallet->hasFolder(FOLDER_NAME)) {
        wallet->createFolder(FOLDER_NAME);
    }
    wallet->setFolder(FOLDER_NAME);

    int ret = wallet->writePassword(id, secret);
    delete wallet;

    return (ret == 0) ? Result::Success : Result::WriteFailure;
}

CredentialStore::Result KWalletCredentialStore::readCredential(const QString& id, QString& secret) {
    KWallet::Wallet* wallet =
        KWallet::Wallet::openWallet(KWallet::Wallet::LocalWallet(), 0, KWallet::Wallet::Synchronous);
    if (!wallet) {
        return Result::WalletUnavailable;
    }

    if (!wallet->hasFolder(FOLDER_NAME)) {
        delete wallet;
        return Result::NotFound;
    }
    wallet->setFolder(FOLDER_NAME);

    if (!wallet->hasEntry(id)) {
        delete wallet;
        return Result::NotFound;
    }

    int ret = wallet->readPassword(id, secret);
    delete wallet;

    return (ret == 0) ? Result::Success : Result::ReadFailure;
}

CredentialStore::Result KWalletCredentialStore::deleteCredential(const QString& id) {
    KWallet::Wallet* wallet =
        KWallet::Wallet::openWallet(KWallet::Wallet::LocalWallet(), 0, KWallet::Wallet::Synchronous);
    if (!wallet) {
        return Result::WalletUnavailable;
    }

    if (!wallet->hasFolder(FOLDER_NAME)) {
        delete wallet;
        return Result::Success; // Already gone
    }
    wallet->setFolder(FOLDER_NAME);

    if (!wallet->hasEntry(id)) {
        delete wallet;
        return Result::Success; // Already gone
    }

    int ret = wallet->removeEntry(id);
    delete wallet;

    return (ret == 0) ? Result::Success : Result::DeleteFailure;
}
