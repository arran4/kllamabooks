#ifndef APPCREDENTIALMANAGER_H
#define APPCREDENTIALMANAGER_H

#include "CredentialStore.h"
#include <QString>
#include <QSettings>

class AppCredentialManager {
public:
    static CredentialStore::Result getCredential(const QString& id, QString& secret, CredentialStore* store = nullptr);
};

#endif // APPCREDENTIALMANAGER_H
