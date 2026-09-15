#ifndef BACKGROUNDJOBSERVICE_H
#define BACKGROUNDJOBSERVICE_H

#include <QList>
#include <QtSql/QSqlDatabase>
#include "group_types.h"

class BackgroundJobService
{
public:
    BackgroundJobService();

    QList<BackgroundJobRecord> loadActiveJobs(QSqlDatabase &db) const;
    bool loadActiveJobForGroup(int id, BackgroundJobRecord &record, QSqlDatabase &db) const;
    bool startPublishingJob(int id, qint64 pid, const QString &executablePath, QSqlDatabase &db) const;
    bool finishPublishingJob(int id, const QString &status, const QString &message, QSqlDatabase &db) const;
    bool markPublishingStopped(int id, const QString &message, QSqlDatabase &db) const;
    int markStalePublishingJobs(QSqlDatabase &db) const;
};

#endif // BACKGROUNDJOBSERVICE_H
