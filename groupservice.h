#ifndef GROUPSERVICE_H
#define GROUPSERVICE_H

#include <QMap>
#include "group.h"

class DatabaseManager;

class GroupService
{
public:
    GroupService();

    const QMap<int, Group> &groups() const;
    bool contains(int id) const;
    Group value(int id) const;
    QList<int> sortedGroupIds() const;
    GroupRuntimeState runtimeState(int id) const;

    void load(DatabaseManager &databaseManager);

    bool add(Group &newGroup, DatabaseManager &databaseManager);
    bool update(const Group &updatedGroup, DatabaseManager &databaseManager);
    bool remove(int id, DatabaseManager &databaseManager);
    bool updateFilterPath(int id, const QString &filterFilePath, DatabaseManager &databaseManager);
    bool setRuntimeState(int id, const GroupRuntimeState &runtimeState);
    bool refreshFromStorage(int id, DatabaseManager &databaseManager);

private:
    QMap<int, Group> allGroups;
    QMap<int, GroupRuntimeState> runtimeStates;
};

#endif // GROUPSERVICE_H
