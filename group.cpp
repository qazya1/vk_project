#include "group.h"

#include <QRegularExpression>

namespace {
int extractVkGroupId(const QString &groupLink)
{
    const QRegularExpression re(R"((?:club|public)(\d+))", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = re.match(groupLink);
    if (match.hasMatch()) {
        return match.captured(1).toInt();
    }
    return 0;
}
}

Group::Group()
{
    groupLink = "";
    groupId = 0;
    id = 0;
    refreshToken = "";
    clientId = "";
    deviceId = "";
    filterFilePath = "";
    name = "";
}

Group::Group(const QString &groupLink, const QString &name, const QString &refreshToken, const QString &clientId, const QString &deviceId)
{
    this->groupLink = groupLink;
    this->groupId = extractVkGroupId(this->groupLink);
    this->id = 0;
    this->refreshToken = refreshToken;
    this->clientId = clientId;
    this->deviceId = deviceId;
    this->filterFilePath = "";
    this->name = name;
}

Group::~Group()
{
}

void Group::setFilterFilePath(const QString &path)
{
    filterFilePath = path;
}

QString Group::getFilterFilePath() const
{
    return filterFilePath;
}

GroupDto Group::toDto() const
{
    GroupDto dto;
    dto.id = id;
    dto.groupId = groupId;
    dto.name = name;
    dto.groupLink = groupLink;
    dto.refreshToken = refreshToken;
    dto.clientId = clientId;
    dto.deviceId = deviceId;
    dto.filterFilePath = filterFilePath;
    return dto;
}

void Group::applyDto(const GroupDto &dto)
{
    id = dto.id;
    name = dto.name;
    groupId = dto.groupId > 0 ? dto.groupId : extractVkGroupId(dto.groupLink);
    groupLink = dto.groupLink;
    refreshToken = dto.refreshToken;
    clientId = dto.clientId;
    deviceId = dto.deviceId;
    filterFilePath = dto.filterFilePath;
}

Group Group::fromDto(const GroupDto &dto)
{
    Group result;
    result.applyDto(dto);
    return result;
}
