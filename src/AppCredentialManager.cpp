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

    // Check fallback to QSettings for unmigrated legacy credentials or ones that failed to migrate
    QSettings settings;
    QVariantList connections = settings.value("llmConnections").toList();
    for (const QVariant& v : connections) {
        QVariantMap map = v.toMap();
        if (map["id"].toString() == id) {
            if (map.contains("authKey") && !map.value("hasCredential", false).toBool()) {
                secret = map["authKey"].toString();
                if (!secret.isEmpty()) {
                    return CredentialStore::Result::Success;
                }
            }
            break;
        }
    }

    // If no fallback was available, return the original failure (e.g. WalletUnavailable)
    // so the caller can distinguish missing vs broken.
    return res;
}
