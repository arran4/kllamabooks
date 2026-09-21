#ifndef APPCREDENTIALMANAGER_H
#define APPCREDENTIALMANAGER_H

#include <QSettings>
#include <QString>

#include "CredentialStore.h"

class AppCredentialManager {
   public:
    static CredentialStore::Result getCredential(const QString& id, QString& secret, CredentialStore* store = nullptr);
};

#endif  // APPCREDENTIALMANAGER_H
