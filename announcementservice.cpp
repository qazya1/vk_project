#include "announcementservice.h"

#include <QDebug>
#include <QFile>
#include <QTextStream>

#include "databasemanager.h"
#include "xmlimportservice.h"
#include "xlsxdocument.h"

AnnouncementService::AnnouncementService(DatabaseManager &databaseManagerRef, XmlImportService &xmlImportServiceRef)
    : databaseManager(databaseManagerRef)
    , xmlImportService(xmlImportServiceRef)
{
}

bool AnnouncementService::importXml(const QString &xmlFilePath) const
{
    return xmlImportService.importAnnouncements(xmlFilePath);
}

bool AnnouncementService::exportGroup(const QString &basePath, int groupId) const
{
    return databaseManager.exportAnnouncements(basePath, groupId);
}

bool AnnouncementService::clearAll() const
{
    return databaseManager.clearAnnouncements();
}

bool AnnouncementService::updateValue(int id, const QString &column, const QString &value) const
{
    return databaseManager.updateAnnouncementValue(id, column, value);
}

QVector<QVector<QVariant> > AnnouncementService::getPage(const AnnouncementQueryParams &params,
                                                         int &numRecords,
                                                         const QString &sortColumnName) const
{
    return databaseManager.queryAnnouncements(params.groupId,
                                              numRecords,
                                              params.columnFilters,
                                              params.limit,
                                              params.page,
                                              params.dateStart,
                                              params.dateFinish,
                                              params.status,
                                              params.id,
                                              sortColumnName,
                                              params.sortAsc,
                                              params.showMergedVacancy,
                                              params.showMergedAccount,
                                              params.allowedFirstWords);
}

QStringList AnnouncementService::loadFirstWordsFromFile(const QString &filePath) const
{
    QStringList firstWords;

    if (filePath.isEmpty() || !QFile::exists(filePath)) {
        return firstWords;
    }

    if (filePath.endsWith(".xlsx", Qt::CaseInsensitive)) {
        QXlsx::Document xlsx(filePath);
        int maxRow = xlsx.dimension().lastRow();
        int maxCol = xlsx.dimension().lastColumn();

        int vacancyCol = -1;
        for (int col = 1; col <= maxCol; ++col) {
            std::shared_ptr<QXlsx::Cell> cell = xlsx.cellAt(1, col);
            if (cell) {
                QString header = cell->value().toString().toLower().trimmed();
                if (header.contains("ваканс") || header.contains("vacancy") ||
                    header.contains("название") || header.contains("должность")) {
                    vacancyCol = col;
                    break;
                }
            }
        }

        int startRow = 2;
        if (vacancyCol == -1 && maxCol > 0) {
            vacancyCol = 1;
            startRow = 1;
        }

        if (vacancyCol > 0) {
            for (int row = startRow; row <= maxRow; ++row) {
                std::shared_ptr<QXlsx::Cell> cell = xlsx.cellAt(row, vacancyCol);
                if (cell) {
                    QString vacancy = cell->value().toString().trimmed();
                    if (!vacancy.isEmpty()) {
                        QString firstWord = vacancy.split(' ', Qt::SkipEmptyParts).first();
                        firstWords.append(firstWord.toLower());
                    }
                }
            }
        }
    } else if (filePath.endsWith(".txt", Qt::CaseInsensitive)) {
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream stream(&file);
            stream.setCodec("UTF-8");

            while (!stream.atEnd()) {
                QString line = stream.readLine().trimmed();
                if (!line.isEmpty()) {
                    QString firstWord = line.split(' ', Qt::SkipEmptyParts).first();
                    firstWords.append(firstWord.toLower());
                }
            }
            file.close();
        }
    }

    firstWords.removeDuplicates();
    qDebug() << "Загружено первых слов из файла" << filePath << ":" << firstWords.size();
    return firstWords;
}
