#include "ConnectionMigration.h"

#include <QDebug>
#include <QUuid>
#include <QVariantMap>

bool ConnectionMigration::migrate(QVariantList& connections, CredentialStore& store, QStringList& errorMessages) {
    bool needsSave = false;

    for (int i = 0; i < connections.size(); ++i) {
        QVariantMap map = connections[i].toMap();

        if (!map.contains("id") || map["id"].toString().isEmpty()) {
            map["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
            needsSave = true;
        }

        QString id = map["id"].toString();

        if (map.contains("authKey")) {
            QString plaintextAuthKey = map["authKey"].toString();
            if (!plaintextAuthKey.isEmpty()) {
                CredentialStore::Result res = store.writeCredential(id, plaintextAuthKey);
                if (res == CredentialStore::Result::Success) {
                    map.remove("authKey");
                    map["hasCredential"] = true;
                    needsSave = true;
                } else {
                    errorMessages.append(QString("Failed to migrate credential for connection '%1' to KWallet.")
                                             .arg(map["name"].toString()));
                    // IMPORTANT: Do NOT remove authKey or set hasCredential if migration fails.
                }
            } else {
                map.remove("authKey");
                map["hasCredential"] = false;
                needsSave = true;
            }
        }
        connections[i] = map;
    }

    return needsSave;
}
