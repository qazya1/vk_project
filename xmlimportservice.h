#ifndef XMLIMPORTSERVICE_H
#define XMLIMPORTSERVICE_H

#include <QString>
#include <QtSql/QSqlDatabase>

class DatabaseManager;

class XmlImportService
{
public:
    explicit XmlImportService(DatabaseManager &databaseManager);

    bool importAnnouncements(const QString &xmlFilePath) const;
    static bool importAnnouncementsToDatabase(const QString &xmlFilePath, QSqlDatabase &db);

private:
    DatabaseManager &databaseManager;
};

#endif // XMLIMPORTSERVICE_H
