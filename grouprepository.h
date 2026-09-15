#ifndef GROUPREPOSITORY_H
#define GROUPREPOSITORY_H

#include <QMap>
#include "group.h"

class DatabaseManager;

namespace GroupRepository {

QMap<int, Group> loadAll(DatabaseManager &databaseManager);
bool add(Group &newGroup, DatabaseManager &databaseManager);
bool update(const Group &updatedGroup, DatabaseManager &databaseManager);
bool remove(int id, DatabaseManager &databaseManager);
bool refresh(int id, DatabaseManager &databaseManager, Group &loadedGroup);

}

#endif // GROUPREPOSITORY_H
