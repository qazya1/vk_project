#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QDate>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>
#include <QtSql/QSqlDatabase>

#include "group.h"

class DatabaseManager
{
public:
    DatabaseManager();
    ~DatabaseManager();

    QSqlDatabase &database();
    const QSqlDatabase &database() const;
    bool open(const QString &dbPath);
    void close();

    bool importAnnouncementsXml(const QString &xmlFilePath);
    QVector<QVector<QVariant> > queryAnnouncements(int groupId,
                                                   int &numRecords,
                                                   const QMap<QString, QString> &columnsFilters,
                                                   int limit,
                                                   int page,
                                                   const QDate &dateStart,
                                                   const QDate &dateFinish,
                                                   const QString &status,
                                                   int id,
                                                   const QString &columnSort,
                                                   bool sortAsc,
                                                   bool showMergedVacancy,
                                                   bool showMergedAccount,
                                                   const QStringList &allowedFirstWords);
    bool exportAnnouncements(const QString &basePath, int groupId);
    bool updateAnnouncementValue(int id, const QString &column, const QString &value);
    bool clearAnnouncements();

    bool insertGroup(const Group &groupData);
    bool updateGroup(const Group &groupData);
    void deleteGroup(int id);
    QMap<int, Group> loadGroups();
    bool loadGroup(int id, Group &loadedGroup);
    int nextGroupId();

private:
    QSqlDatabase db;
};

#endif // DATABASEMANAGER_H
