#include "groupservice.h"
#include <algorithm>
#include "grouprepository.h"
#include "databasemanager.h"

GroupService::GroupService()
{
}

const QMap<int, Group> &GroupService::groups() const
{
    return allGroups;
}

bool GroupService::contains(int id) const
{
    return allGroups.contains(id);
}

Group GroupService::value(int id) const
{
    return allGroups.value(id);
}

QList<int> GroupService::sortedGroupIds() const
{
    QList<int> ids = allGroups.keys();
    std::sort(ids.begin(), ids.end());
    return ids;
}

GroupRuntimeState GroupService::runtimeState(int id) const
{
    return runtimeStates.value(id);
}

void GroupService::load(DatabaseManager &databaseManager)
{
    allGroups = GroupRepository::loadAll(databaseManager);
    runtimeStates.clear();
    for (QMap<int, Group>::const_iterator it = allGroups.constBegin(); it != allGroups.constEnd(); ++it) {
        runtimeStates.insert(it.key(), GroupRuntimeState());
    }
}

bool GroupService::add(Group &newGroup, DatabaseManager &databaseManager)
{
    if (!GroupRepository::add(newGroup, databaseManager)) {
        return false;
    }
    allGroups.insert(newGroup.id, newGroup);
    runtimeStates.insert(newGroup.id, GroupRuntimeState());
    return true;
}

bool GroupService::update(const Group &updatedGroup, DatabaseManager &databaseManager)
{
    if (!GroupRepository::update(updatedGroup, databaseManager)) {
        return false;
    }
    allGroups.insert(updatedGroup.id, updatedGroup);
    if (!runtimeStates.contains(updatedGroup.id)) {
        runtimeStates.insert(updatedGroup.id, GroupRuntimeState());
    }
    return true;
}

bool GroupService::remove(int id, DatabaseManager &databaseManager)
{
    if (!allGroups.contains(id)) {
        return false;
    }
    if (!GroupRepository::remove(id, databaseManager)) {
        return false;
    }
    allGroups.remove(id);
    runtimeStates.remove(id);
    return true;
}

bool GroupService::updateFilterPath(int id, const QString &filterFilePath, DatabaseManager &databaseManager)
{
    if (!allGroups.contains(id)) {
        return false;
    }
    Group updatedGroup = allGroups.value(id);
    updatedGroup.setFilterFilePath(filterFilePath);
    if (!GroupRepository::update(updatedGroup, databaseManager)) {
        return false;
    }
    allGroups.insert(id, updatedGroup);
    return true;
}

bool GroupService::setRuntimeState(int id, const GroupRuntimeState &runtimeState)
{
    if (!allGroups.contains(id)) {
        return false;
    }
    runtimeStates.insert(id, runtimeState);
    return true;
}

bool GroupService::refreshFromStorage(int id, DatabaseManager &databaseManager)
{
    if (!allGroups.contains(id)) {
        return false;
    }
    Group refreshedGroup;
    if (!GroupRepository::refresh(id, databaseManager, refreshedGroup)) {
        return false;
    }
    allGroups.insert(id, refreshedGroup);
    if (!runtimeStates.contains(id)) {
        runtimeStates.insert(id, GroupRuntimeState());
    }
    return true;
}
