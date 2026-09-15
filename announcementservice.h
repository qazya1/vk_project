#ifndef ANNOUNCEMENTSERVICE_H
#define ANNOUNCEMENTSERVICE_H

#include <QDate>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

class DatabaseManager;
class XmlImportService;

struct AnnouncementQueryParams
{
    int groupId;
    int page;
    int limit;
    int id;
    int columnSort;
    bool sortAsc;
    QDate dateStart;
    QDate dateFinish;
    QString status;
    bool showMergedVacancy;
    bool showMergedAccount;
    QMap<QString, QString> columnFilters;
    QStringList allowedFirstWords;
};

class AnnouncementService
{
public:
    explicit AnnouncementService(DatabaseManager &databaseManager, XmlImportService &xmlImportService);

    bool importXml(const QString &xmlFilePath) const;
    bool exportGroup(const QString &basePath, int groupId) const;
    bool clearAll() const;
    bool updateValue(int id, const QString &column, const QString &value) const;

    QVector<QVector<QVariant> > getPage(const AnnouncementQueryParams &params,
                                        int &numRecords,
                                        const QString &sortColumnName) const;

    QStringList loadFirstWordsFromFile(const QString &filePath) const;

private:
    DatabaseManager &databaseManager;
    XmlImportService &xmlImportService;
};

#endif // ANNOUNCEMENTSERVICE_H
