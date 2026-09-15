#include "announcementtableformatter.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QTableWidget>
#include <QTableWidgetItem>

#include "mainwindow.h"
#include "table_ui_utils.h"

TableConfigurator::TableDefinition AnnouncementTableFormatter::buildTableDefinition()
{
    TableConfigurator::TableDefinition definition;
    definition.columns.resize(23);
    definition.columns[0].delegateKind = TableConfigurator::HtmlDelegateKind;
    definition.columns[4].delegateKind = TableConfigurator::HtmlDelegateKind;
    definition.columns[5].delegateKind = TableConfigurator::HtmlDelegateKind;
    definition.hover.columns = QVector<int>() << 3 << 22 << 23;
    definition.searchRow.enabled = true;
    definition.searchRow.row = 0;
    return definition;
}

void AnnouncementTableFormatter::setupTable(QTableWidget *table, MainWindow *mainWindow)
{
    TableConfigurator::apply(table, buildTableDefinition(), mainWindow);
}

void AnnouncementTableFormatter::populateRows(QTableWidget *table,
                                              const QVector<QVector<QVariant> > &data,
                                              const QMap<int, QString> &columnNames)
{
    if (!table) {
        return;
    }

    for (int row = table->rowCount(); row > 0; --row) {
        table->removeRow(row);
    }

    for (int rowIndex = 0; rowIndex < data.size(); ++rowIndex) {
        table->insertRow(rowIndex + 1);
        for (int columnIndex = 0; columnIndex < 23; ++columnIndex) {
            QTableWidgetItem *item = createFormattedItem(data[rowIndex], rowIndex, columnIndex, columnNames, table);
            table->setItem(rowIndex + 1, columnIndex, item);
        }
        table->setRowHeight(rowIndex + 1, 71);
    }
}

void AnnouncementTableFormatter::toggleMarkers(QTableWidget *table, bool enabled)
{
    TableConfigurator::HtmlToggleConfig config;
    config.columns = QVector<int>() << 16 << 17 << 18;
    TableConfigurator::toggleHtmlDisplayForColumns(table, enabled, config);
}

QTableWidgetItem *AnnouncementTableFormatter::createFormattedItem(const QVector<QVariant> &rowData,
                                                                  int rowIndex,
                                                                  int columnIndex,
                                                                  const QMap<int, QString> &columnNames,
                                                                  QTableWidget *table)
{
    QTableWidgetItem* item = new QTableWidgetItem();
    QFont font;
    font.setFamily("Roboto Light");
    font.setPixelSize(20);
    item->setFont(font);
    item->setTextAlignment(Qt::AlignTop);

    const QString style = "font-size: 20px;font-family: \"Roboto Light\";";
    QString coloredText;

    switch (columnIndex) {
    case 0:
        coloredText = QString("<p style='%1'><font color='#62560E'>%2<br></font><font color='#0087FC'>%3</font></p>")
                .arg(style)
                .arg(rowIndex + 1)
                .arg(rowData.value(columnIndex).toString());
        item->setData(Qt::DisplayRole, coloredText);
        item->setData(Qt::UserRole, rowData.value(columnIndex));
        if (table->item(rowIndex, columnIndex)) {
            item->setFlags(table->item(rowIndex, columnIndex)->flags() & ~Qt::ItemIsEditable);
        }
        break;
    case 4:
        if (!rowData.value(23).toString().isEmpty()) {
            coloredText = QString("<p style='%1'><font color='#62560E'>%2<br></font><font color='#0087FC'>%3</font></p>")
                    .arg(style)
                    .arg(convertDateFormat(rowData.value(columnIndex).toString()))
                    .arg(convertDateFormat(rowData.value(23).toString(), true));
            item->setData(Qt::DisplayRole, coloredText);
            item->setData(Qt::UserRole, rowData.value(columnIndex));
            if (table->item(rowIndex, columnIndex)) {
                item->setFlags(table->item(rowIndex, columnIndex)->flags() & ~Qt::ItemIsEditable);
            }
        } else {
            item->setData(Qt::DisplayRole, convertDateFormat(rowData.value(columnIndex).toString()));
        }
        break;
    case 5:
        if (!rowData.value(24).toString().isEmpty()) {
            coloredText = QString("<p style='%1'><font color='#62560E'>%2<br></font><font color='red'>%3</font></p>")
                    .arg(style)
                    .arg(convertDateFormat(rowData.value(columnIndex).toString()))
                    .arg(convertDateFormat(rowData.value(24).toString(), true));
            item->setData(Qt::DisplayRole, coloredText);
            item->setData(Qt::UserRole, rowData.value(columnIndex));
            if (table->item(rowIndex, columnIndex)) {
                item->setFlags(table->item(rowIndex, columnIndex)->flags() & ~Qt::ItemIsEditable);
            }
        } else {
            item->setData(Qt::DisplayRole, convertDateFormat(rowData.value(columnIndex).toString()));
        }
        break;
    default:
        if (columnNames.value(columnIndex) == "account_date") {
            item->setData(Qt::DisplayRole, convertDateFormat(rowData.value(columnIndex).toString()));
        } else {
            item->setData(Qt::DisplayRole, rowData.value(columnIndex));
        }
        break;
    }

    if (columnIndex == 3 || columnIndex >= 21) {
        item->setForeground(QBrush(QColor("#0087FC")));
    } else {
        item->setForeground(QBrush(QColor("#62560E")));
    }

    return item;
}
