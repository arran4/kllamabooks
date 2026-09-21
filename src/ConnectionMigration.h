#ifndef CONNECTIONMIGRATION_H
#define CONNECTIONMIGRATION_H

#include <QStringList>
#include <QVariantList>

#include "CredentialStore.h"

class ConnectionMigration {
   public:
    static bool migrate(QVariantList& connections, CredentialStore& store, QStringList& errorMessages);
};

#endif  // CONNECTIONMIGRATION_H
