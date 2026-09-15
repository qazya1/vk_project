#include "backgroundjobservice.h"

#include <QDateTime>
#include <QSqlQuery>
#include <QVariant>

BackgroundJobService::BackgroundJobService()
{
}

QList<BackgroundJobRecord> BackgroundJobService::loadActiveJobs(QSqlDatabase &db) const
{
    QList<BackgroundJobRecord> jobs;
    QSqlQuery query(db);
    query.prepare("SELECT group_record_id, task_type, status, process_id, started_at, finished_at, executable_path, detached FROM background_jobs WHERE task_type='publishing' AND status='running' ORDER BY group_record_id");
    if (!query.exec()) { return jobs; }
    while (query.next()) {
        BackgroundJobRecord record;
        record.groupRecordId = query.value(0).toInt();
        record.taskType = query.value(1).toString();
        record.status = query.value(2).toString();
        record.processId = query.value(3).toLongLong();
        record.startedAt = query.value(4).toString();
        record.finishedAt = query.value(5).toString();
        record.executablePath = query.value(6).toString();
        record.detached = query.value(7).toInt() != 0;
        jobs.append(record);
    }
    return jobs;
}

bool BackgroundJobService::loadActiveJobForGroup(int id, BackgroundJobRecord &record, QSqlDatabase &db) const
{
    QSqlQuery query(db);
    query.prepare("SELECT group_record_id, task_type, status, process_id, started_at, finished_at, executable_path, detached FROM background_jobs WHERE group_record_id=:group_record_id AND task_type='publishing' AND status='running' LIMIT 1");
    query.bindValue(":group_record_id", id);
    if (!query.exec() || !query.next()) return false;
    record.groupRecordId = query.value(0).toInt();
    record.taskType = query.value(1).toString();
    record.status = query.value(2).toString();
    record.processId = query.value(3).toLongLong();
    record.startedAt = query.value(4).toString();
    record.finishedAt = query.value(5).toString();
    record.executablePath = query.value(6).toString();
    record.detached = query.value(7).toInt() != 0;
    return true;
}

bool BackgroundJobService::startPublishingJob(int id, qint64 pid, const QString &executablePath, QSqlDatabase &db) const
{
    QSqlQuery query(db);
    query.prepare("INSERT INTO background_jobs (group_record_id, task_type, status, process_id, started_at, executable_path, detached) VALUES (:group_record_id, 'publishing', 'running', :process_id, :started_at, :executable_path, 1) ON CONFLICT(group_record_id, task_type) DO UPDATE SET status='running', process_id=excluded.process_id, started_at=excluded.started_at, finished_at=NULL, last_error='', executable_path=excluded.executable_path, detached=1");
    query.bindValue(":group_record_id", id);
    query.bindValue(":process_id", pid);
    query.bindValue(":started_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    query.bindValue(":executable_path", executablePath);
    return query.exec();
}

bool BackgroundJobService::finishPublishingJob(int id, const QString &status, const QString &message, QSqlDatabase &db) const
{
    QSqlQuery query(db);
    query.prepare("UPDATE background_jobs SET status=:status, finished_at=:finished_at, last_error=:last_error WHERE group_record_id=:group_record_id AND task_type='publishing'");
    query.bindValue(":status", status);
    query.bindValue(":finished_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    query.bindValue(":last_error", message);
    query.bindValue(":group_record_id", id);
    return query.exec();
}

bool BackgroundJobService::markPublishingStopped(int id, const QString &message, QSqlDatabase &db) const
{
    return finishPublishingJob(id, QStringLiteral("stopped"), message, db);
}

int BackgroundJobService::markStalePublishingJobs(QSqlDatabase &db) const
{
    QSqlQuery query(db);
    query.prepare("UPDATE background_jobs SET status='stale', finished_at=:finished_at, last_error='Помечено как устаревшая задача' WHERE task_type='publishing' AND status='running'");
    query.bindValue(":finished_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (!query.exec()) return 0;
    return query.numRowsAffected();
}
