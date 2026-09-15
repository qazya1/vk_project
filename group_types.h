#ifndef GROUP_TYPES_H
#define GROUP_TYPES_H

#include <QString>
#include <QMetaType>
#include <QMap>

struct GroupDto
{
    int id = 0;
    int groupId = 0;
    QString groupLink;
    QString name;
    QString refreshToken;
    QString clientId;
    QString deviceId;
    QString filterFilePath;
};

struct GroupRuntimeState
{
    qint64 processId = -1;
};

struct BackgroundJobRecord
{
    int groupRecordId = 0;
    QString taskType;
    QString status;
    qint64 processId = -1;
    QString startedAt;
    QString finishedAt;
    QString executablePath;
    bool detached = true;
};

struct PublishSettings
{
    int postInterval = 10;
    QString startTime = "08:00";
    QString endTime = "20:00";
    bool roundTheClock = false;
    bool repostEnabled = true;
    int repostIntervalDays = 3;
    bool mergeVacancies = true;
    bool mergeByNumber = true;
    bool hideCompanyNames = false;
    int salaryThreshold = 50000;
    bool hideSalary = false;
    QString vacancyFilter;
    bool hideAdditionalInfo = false;
    bool hideAddress = false;
    bool deleteAllPosts = false;
    bool mergePeriod = false;
    int daysMergePeriod = 0;
};

struct TableUiState
{
    int currentPage = 1;
    int currentColumnSort = 0;
    bool currentSortAsc = true;
    int currentGroupId = 0;
    bool tableHeadersSortAsc = true;
    int lastTableHeaderSort = 0;
    int totalPages = 0;
    bool markersEnabled = false;
};

struct GroupsUiState
{
    QMap<int, bool> collapsed;
};

struct GroupWidgetViewModel
{
    GroupDto group;
    GroupRuntimeState runtimeState;
    bool collapsed = false;
    bool publishing = false;
    bool clearingWall = false;
    bool stopping = false;
};

struct PublishJobRequest
{
    GroupDto group;
    GroupRuntimeState runtimeState;
    PublishSettings settings;
    QString dbPath;
};

Q_DECLARE_METATYPE(GroupDto)
Q_DECLARE_METATYPE(GroupRuntimeState)
Q_DECLARE_METATYPE(GroupWidgetViewModel)
Q_DECLARE_METATYPE(PublishSettings)
Q_DECLARE_METATYPE(PublishJobRequest)
Q_DECLARE_METATYPE(BackgroundJobRecord)

#endif // GROUP_TYPES_H
