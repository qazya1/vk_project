#include "grouprepository.h"

#include "databasemanager.h"

namespace GroupRepository {

QMap<int, Group> loadAll(DatabaseManager &databaseManager)
{
    return databaseManager.loadGroups();
}

bool add(Group &newGroup, DatabaseManager &databaseManager)
{
    if (newGroup.id <= 0) {
        newGroup.id = databaseManager.nextGroupId();
    }
    return databaseManager.insertGroup(newGroup);
}

bool update(const Group &updatedGroup, DatabaseManager &databaseManager)
{
    return databaseManager.updateGroup(updatedGroup);
}

bool remove(int id, DatabaseManager &databaseManager)
{
    databaseManager.deleteGroup(id);
    return true;
}

bool refresh(int id, DatabaseManager &databaseManager, Group &loadedGroup)
{
    return databaseManager.loadGroup(id, loadedGroup);
}

}
