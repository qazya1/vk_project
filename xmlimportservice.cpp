#include "xmlimportservice.h"

#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>

#include "databasemanager.h"
#include "pugixml/pugixml.hpp"

namespace {

void buildAddressString(const pugi::xml_node &addressNode,
                        QMap<QString, QString> *addressParts,
                        QString &houses,
                        const QString &parentName)
{
    const QString oblast = QString::fromStdString(addressNode.child_value((parentName + "-OBLAST").toStdString().c_str()));
    const QString tipOblast = QString::fromStdString(addressNode.child_value((parentName + "-TIPOBLAST").toStdString().c_str()));

    if (!oblast.isEmpty()) {
        addressParts->insert("Область", tipOblast.isEmpty() ? oblast : oblast + " " + tipOblast);
    }

    const QString oblastRaion = QString::fromStdString(addressNode.child_value((parentName + "-OBLAST-RAION").toStdString().c_str()));
    if (!oblastRaion.isEmpty()) {
        addressParts->insert("Район области", oblastRaion + " район");
    }

    const QString tipDervny = QString::fromStdString(addressNode.child_value((parentName + "-OBLAST-TIPDERVNY").toStdString().c_str()));
    const QString dervny = QString::fromStdString(addressNode.child_value((parentName + "-OBLAST-DERVNY").toStdString().c_str()));
    if (!dervny.isEmpty()) {
        addressParts->insert("Деревня", tipDervny.isEmpty() ? dervny : tipDervny + " " + dervny);
    }

    const QString gorod = QString::fromStdString(addressNode.child_value((parentName + "-GOROD").toStdString().c_str()));
    if (!gorod.isEmpty()) {
        addressParts->insert("Город", "г. " + gorod);
    } else {
        const QString gorodTipDervny = QString::fromStdString(addressNode.child_value((parentName + "-GOROD-TIPDERVNY").toStdString().c_str()));
        const QString gorodDervny = QString::fromStdString(addressNode.child_value((parentName + "-GOROD-DERVNY").toStdString().c_str()));
        if (!gorodDervny.isEmpty()) {
            addressParts->insert("Город", gorodTipDervny.isEmpty() ? gorodDervny : gorodTipDervny + " " + gorodDervny);
        }
    }

    const QString raionGoroda = QString::fromStdString(addressNode.child_value((parentName + "-RAIONGORODA").toStdString().c_str()));
    if (!raionGoroda.isEmpty()) {
        addressParts->insert("Район города", raionGoroda + " район");
    }

    const QString tipUlica = QString::fromStdString(addressNode.child_value((parentName + "-TIPULICA").toStdString().c_str()));
    const QString ulica = QString::fromStdString(addressNode.child_value((parentName + "-ULICA").toStdString().c_str()));
    if (!ulica.isEmpty()) {
        addressParts->insert("Улица", tipUlica.isEmpty() ? ulica : tipUlica + " " + ulica);
    }

    const QString dom = QString::fromStdString(addressNode.child_value((parentName + "-DOM").toStdString().c_str()));
    if (!dom.isEmpty()) {
        addressParts->insert("Дом", "дом " + dom);
        houses = "д. " + dom;
    }
}

QString uniteAddresses(const QList<QMap<QString, QString> *> &allAddressesParts)
{
    QSet<int> usedIndexes;
    for (int i = 0; i < allAddressesParts.length(); ++i) {
        if (usedIndexes.contains(i)) {
            continue;
        }
        usedIndexes.insert(i);
        QMap<QString, QString> *addressParts = allAddressesParts[i];
        if (!addressParts->contains("Город")) {
            continue;
        }

        const QString neededCity = addressParts->value("Город");
        QString addressString = "Работа в нескольких районах " + neededCity + ": ";
        QStringList allAddresses;
        for (int j = 0; j < allAddressesParts.length(); ++j) {
            if (!usedIndexes.contains(j) && allAddressesParts[j]->value("Город") == neededCity) {
                usedIndexes.insert(j);
                allAddresses << allAddressesParts[j]->value("Район города", ", ");
            }
        }
        addressString += allAddresses.join(", ").replace(" район", "");
        return addressString;
    }
    return QString();
}

void buildScheduleString(const pugi::xml_node &scheduleNode, QString &schedule)
{
    QString hours;
    QString days;
    QString shift;

    const QRegularExpression hoursRegex("\\d{1,2}:\\d{1,2}-\\d{1,2}:\\d{1,2}");
    const QRegularExpression daysRegex("\\d/\\d");

    for (pugi::xml_node gafik = scheduleNode.child("GAFIK"); gafik; gafik = gafik.next_sibling("GAFIK")) {
        const QString numb = QString::fromUtf8(gafik.attribute("NUMB").value());

        bool hasChildElements = false;
        for (pugi::xml_node child = gafik.first_child(); child; child = child.next_sibling()) {
            if (child.type() == pugi::node_element) {
                hasChildElements = true;
                break;
            }
        }

        if (hasChildElements) {
            const QString smenaName = "SMENA" + numb;
            const QString dniName = "DNI_RABOTI" + numb;
            const QString vremyaName = "VREMYARABOTY" + numb;

            const pugi::xml_node smenaNode = gafik.child(smenaName.toUtf8().constData());
            if (smenaNode) {
                shift = QString::fromUtf8(smenaNode.child_value());
            }

            const pugi::xml_node dniNode = gafik.child(dniName.toUtf8().constData());
            if (dniNode) {
                days = QString::fromUtf8(dniNode.child_value());
            }

            const pugi::xml_node vremyaNode = gafik.child(vremyaName.toUtf8().constData());
            if (vremyaNode) {
                hours = QString::fromUtf8(vremyaNode.child_value());
            }
        } else {
            const QString gafikValue = QString::fromUtf8(gafik.text().get());
            if (daysRegex.match(gafikValue).hasMatch()) {
                days = gafikValue;
            } else if (hoursRegex.match(gafikValue).hasMatch()) {
                hours = hours.isEmpty() ? gafikValue : hours + ", есть ещё вариант " + gafikValue;
            }
        }
    }

    if (!hours.isEmpty() && !days.isEmpty() && !shift.isEmpty()) {
        schedule = "График работы: смена " + shift + ", дни работы " + days + ", время работы " + hours;
    } else if (!hours.isEmpty() && !days.isEmpty()) {
        schedule = "График работы: дни работы " + days + ", время работы " + hours;
    } else if (hours.isEmpty() && !days.isEmpty()) {
        schedule = "График работы: " + days;
    } else if (days.isEmpty() && !hours.isEmpty()) {
        schedule = "Время работы " + hours;
    } else {
        schedule.clear();
    }
}

QString dateFromSeconds(const QString &numSeconds)
{
    const qint64 secondsSinceEpoch = numSeconds.toLongLong();
    const QDateTime dateTime = QDateTime::fromSecsSinceEpoch(secondsSinceEpoch, Qt::UTC);
    return dateTime.toLocalTime().date().toString("yyyy-MM-dd");
}

QString normalizePhone(QString phone)
{
    static const QStringList phonePrefixes = {
        "883130", "883136", "883138", "883139", "883140", "883143", "883144", "883145",
        "883147", "883148", "883149", "883150", "883151", "883152", "883153", "883154",
        "883155", "883156", "883157", "883158", "883159", "883160", "883161", "883162",
        "883163", "883164", "883165", "883166", "883167", "883168", "883169", "883170",
        "883171", "883172", "883173", "883174", "883175", "883176", "883177", "883178",
        "883179", "883180", "883181", "883182", "883183", "883184", "883185", "883186",
        "883187", "883188", "883189", "883190", "883191", "883192", "883193", "883194",
        "883195", "883196", "883197", "883198", "883199"
    };

    phone.remove(QRegularExpression("[^\\d+]"));
    if (phone.startsWith("8")) {
        phone.remove(0, 1);
    }
    for (const QString &prefix : phonePrefixes) {
        if (phone.startsWith(prefix)) {
            phone.insert(prefix.length(), " ");
            break;
        }
    }
    return phone;
}

QString extractPhoneNumbers(const pugi::xml_node &row, const QMap<QString, QString> &rowData)
{
    if (!rowData.value("TELEF").isEmpty()) {
        return normalizePhone(rowData.value("TELEF"));
    }

    QString phoneNumbers;
    for (pugi::xml_node child : row.children()) {
        const QString name = QString::fromStdString(child.name());
        if (name != "TELEF") {
            continue;
        }
        for (pugi::xml_node childTelef : child.children()) {
            if (QString::fromStdString(childTelef.name()) == "TELEF_NOMER") {
                if (!phoneNumbers.isEmpty()) {
                    phoneNumbers += ", ";
                }
                phoneNumbers += normalizePhone(QString::fromStdString(childTelef.child_value()));
            }
        }
    }
    return phoneNumbers;
}

QString buildUnifiedAddress(const QList<QMap<QString, QString> *> &allAddressesParts)
{
    if (allAddressesParts.length() > 1) {
        return uniteAddresses(allAddressesParts);
    }

    QMap<QString, QString> *addressParts = allAddressesParts.value(0);
    if (!addressParts) {
        return QString();
    }

    QString address = addressParts->value("Область", "") + ", "
                      + addressParts->value("Район области", "") + ", "
                      + addressParts->value("Деревня", "") + ", "
                      + addressParts->value("Город", "") + ", "
                      + addressParts->value("Район города", "") + ", "
                      + addressParts->value("Улица", "") + ", "
                      + addressParts->value("Дом", "");
    return address.replace(QRegularExpression(",\\s*,|\\s*,\\s*$|^\\s*,"), "");
}

}

XmlImportService::XmlImportService(DatabaseManager &databaseManagerRef)
    : databaseManager(databaseManagerRef)
{
}

bool XmlImportService::importAnnouncements(const QString &xmlFilePath) const
{
    return importAnnouncementsToDatabase(xmlFilePath, databaseManager.database());
}

bool XmlImportService::importAnnouncementsToDatabase(const QString &xmlFilePath, QSqlDatabase &db)
{
    if (!db.isOpen()) {
        return false;
    }

    QFile file(xmlFilePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    stream.setCodec("cp1251");
    pugi::xml_document doc;
    const pugi::xml_parse_result result = doc.load(stream.readAll().toStdString().c_str());
    if (!result) {
        return false;
    }

    if (!db.transaction()) {
        return false;
    }

    int rowsAdded = 0;
    for (pugi::xml_node row : doc.child("DECLARBODY").children("ROW")) {
        QMap<QString, QString> rowData;
        QString houses;
        QString schedules;
        QList<QMap<QString, QString> *> allAddressesParts;

        for (pugi::xml_node child : row.children()) {
            const QString name = QString::fromStdString(child.name());
            if (name.contains("ADRESSORABOTI")) {
                QString house;
                QMap<QString, QString> *address = new QMap<QString, QString>;
                buildAddressString(child, address, house, name);
                allAddressesParts.push_front(address);
                if (!houses.isEmpty() && !house.isEmpty()) {
                    houses += "\n";
                }
                houses += house;
            } else {
                if (name.contains("GAFIK_RABOTI")) {
                    QString schedule;
                    buildScheduleString(child, schedule);
                    if (!schedules.isEmpty() && !schedule.isEmpty()) {
                        schedules += "\nесть ещё вариант ";
                    }
                    schedules += schedule;
                }
                rowData[name] = QString::fromStdString(child.child_value());
            }
        }

        const QString addresses = buildUnifiedAddress(allAddressesParts);
        const QString phoneNumbers = extractPhoneNumbers(row, rowData);

        QString contactMan = rowData.value("KOGOSPROSITJ");
        if (contactMan.count(" ") == 2) {
            const int index = contactMan.indexOf(" ");
            contactMan = contactMan.right(contactMan.length() - index);
        }

        qDeleteAll(allAddressesParts);
        allAddressesParts.clear();

        QSqlQuery query(db);
        query.prepare("INSERT INTO announcements ("
                      "vacancy, publication_date, depublication_date, "
                      "account_number, account_date, atribute, inn, company, phone, email, "
                      "contact_man, address, house_number, responcibilities, requirements, conditions, "
                      "work_schedule, salary) "
                      "VALUES ("
                      ":vacancy, :publication_date, :depublication_date, "
                      ":account_number, :account_date, :atribute, :inn, :company, :phone, :email, "
                      ":contact_man, :address, :house_number, :responcibilities, :requirements, :conditions, "
                      ":work_schedule, :salary)");

        query.bindValue(":vacancy", rowData.value("VAKNAZV"));
        query.bindValue(":publication_date", dateFromSeconds(rowData.value("PUBLON")));
        query.bindValue(":depublication_date", dateFromSeconds(rowData.value("PUBLOFF")));
        query.bindValue(":account_number", rowData.value("SCHETNOMER"));
        query.bindValue(":account_date", QDate::fromString(rowData.value("SCHETDATA"), "dd.MM.yy").addYears(100).toString("yyyy-MM-dd"));
        query.bindValue(":atribute", rowData.value("RUBR_ATRYB"));
        query.bindValue(":inn", rowData.value("INNKOMPAN"));
        query.bindValue(":company", rowData.value("NAZVKOMPAN"));
        query.bindValue(":phone", phoneNumbers);
        query.bindValue(":email", rowData.value("ELPOCHTA"));
        query.bindValue(":contact_man", contactMan);
        query.bindValue(":address", addresses);
        query.bindValue(":house_number", houses);
        query.bindValue(":responcibilities", rowData.value("DOPINFORMSOBYZANOSTI"));
        query.bindValue(":requirements", rowData.value("DOPINFORMSTREBOVANIY"));
        query.bindValue(":conditions", rowData.value("DOPINFORMSUSLOVIY"));
        query.bindValue(":work_schedule", schedules);
        query.bindValue(":salary", rowData.value("ZARPL"));

        if (!query.exec()) {
            qDebug() << "Failed to insert row:" << query.lastError().text();
            continue;
        }
        ++rowsAdded;
    }

    if (!db.commit()) {
        file.close();
        return false;
    }

    file.close();
    qDebug() << "Successfully added" << rowsAdded << "rows to database";

    QSqlQuery queryPublications(db);
    queryPublications.exec("INSERT OR IGNORE INTO announcement_publications (announcement_id, group_id, status)"
                           "SELECT id, group_id, 'Не опубликованное' FROM announcements CROSS JOIN vk_groups;");
    return true;
}
