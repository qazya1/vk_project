#ifndef ANNOUNCEMENTTABLEFORMATTER_H
#define ANNOUNCEMENTTABLEFORMATTER_H

#include <QMap>
#include <QString>
#include <QVariant>
#include <QVector>

#include "tableconfigurator.h"

class MainWindow;
class QTableWidget;
class QTableWidgetItem;

class AnnouncementTableFormatter
{
public:
    static TableConfigurator::TableDefinition buildTableDefinition();
    static void setupTable(QTableWidget *table, MainWindow *mainWindow);
    static void populateRows(QTableWidget *table,
                             const QVector<QVector<QVariant> > &data,
                             const QMap<int, QString> &columnNames);
    static void toggleMarkers(QTableWidget *table, bool enabled);

private:
    static QTableWidgetItem *createFormattedItem(const QVector<QVariant> &rowData,
                                                 int rowIndex,
                                                 int columnIndex,
                                                 const QMap<int, QString> &columnNames,
                                                 QTableWidget *table);
};

#endif // ANNOUNCEMENTTABLEFORMATTER_H
