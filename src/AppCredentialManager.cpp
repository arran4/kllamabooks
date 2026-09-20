#include "AppCredentialManager.h"
#include <QVariantList>
#include <QVariantMap>
#include <QScopedPointer>

CredentialStore::Result AppCredentialManager::getCredential(const QString& id, QString& secret, CredentialStore* store) {
    QScopedPointer<CredentialStore> defaultStore;
    if (!store) {
        defaultStore.reset(new KWalletCredentialStore());
        store = defaultStore.data();
    }

    CredentialStore::Result res = store->readCredential(id, secret);
    if (res == CredentialStore::Result::Success) {
        return res;
    }
    if (res == CredentialStore::Result::WalletUnavailable || res == CredentialStore::Result::ReadFailure) {
        return res;
    }

    // Fallback to QSettings for unmigrated legacy credentials
    QSettings settings;
    QVariantList connections = settings.value("llmConnections").toList();
    for (const QVariant& v : connections) {
        QVariantMap map = v.toMap();
        if (map["id"].toString() == id) {
            if (map.contains("authKey")) {
                secret = map["authKey"].toString();
                if (!secret.isEmpty()) {
                    return CredentialStore::Result::Success;
                }
            }
            break;
        }
    }

    return CredentialStore::Result::NotFound;
}
