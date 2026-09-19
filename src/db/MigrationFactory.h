#ifndef DB_MIGRATION_FACTORY_H
#define DB_MIGRATION_FACTORY_H

#include "Migrations.h"

namespace db {

class MigrationFactory {
   public:
    static MigrationRunner createRunner();
};

}  // namespace db

#endif  // DB_MIGRATION_FACTORY_H
