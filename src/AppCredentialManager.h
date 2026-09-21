#ifndef APPCREDENTIALMANAGER_H
#define APPCREDENTIALMANAGER_H

#include <QSettings>
#include <QString>
#include <functional>

#include "CredentialStore.h"

class AppCredentialManager {
   public:
    static CredentialStore::Result getCredential(const QString& id, QString& secret, CredentialStore* store = nullptr);

    using StoreFactory = std::function<CredentialStore*()>;
    static void setStoreFactory(StoreFactory factory);
    static StoreFactory getStoreFactory();

   private:
    static StoreFactory s_storeFactory;
};

#endif  // APPCREDENTIALMANAGER_H
