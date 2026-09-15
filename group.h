#ifndef GROUP_H
#define GROUP_H

#include <QString>
#include "group_types.h"

class Group
{
public:
    Group();
    Group(const QString &groupLink, const QString &name, const QString &refreshToken, const QString &clientId, const QString &deviceId);
    ~Group();

    QString groupLink;
    int groupId;
    int id;
    QString refreshToken;
    QString clientId;
    QString deviceId;
    QString filterFilePath;
    QString name;

    void setFilterFilePath(const QString &path);
    QString getFilterFilePath() const;

    GroupDto toDto() const;
    void applyDto(const GroupDto &dto);
    static Group fromDto(const GroupDto &dto);
};

#endif // GROUP_H
