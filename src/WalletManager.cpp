#include "WalletManager.h"

#include <KF6/KWallet/KWallet>

namespace {
const QString FOLDER_NAME = "kllamabooks";
}

QString WalletManager::loadPassword(const QString& bookName) {
    KWallet::Wallet *wallet =
        KWallet::Wallet::openWallet(KWallet::Wallet::LocalWallet(), 0, KWallet::Wallet::Synchronous);
    if (wallet) {
        if (!wallet->hasFolder(FOLDER_NAME)) {
            wallet->createFolder(FOLDER_NAME);
        }
        wallet->setFolder(FOLDER_NAME);
        QString password;
        wallet->readPassword(bookName, password);
        delete wallet;
        return password;
    }
    return QString();
}

void WalletManager::savePassword(const QString& bookName, const QString& password) {
    KWallet::Wallet *wallet =
        KWallet::Wallet::openWallet(KWallet::Wallet::LocalWallet(), 0, KWallet::Wallet::Synchronous);
    if (wallet) {
        if (!wallet->hasFolder(FOLDER_NAME)) {
            wallet->createFolder(FOLDER_NAME);
        }
        wallet->setFolder(FOLDER_NAME);
        wallet->writePassword(bookName, password);
        delete wallet;
    }
}
