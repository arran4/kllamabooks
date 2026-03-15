#ifndef WALLETMANAGER_H
#define WALLETMANAGER_H

#include <QFuture>
#include <QString>

class WalletManager {
   public:
    static QString loadPassword(const QString& bookName);
    static void savePassword(const QString& bookName, const QString& password);
};

#endif  // WALLETMANAGER_H
