#include "table_ui_utils.h"

#include <QHeaderView>
#include <QSettings>
#include <QTableWidget>
#include <QFontMetrics>

void saveTableColumnWidthsToSettings(const QTableWidget* table, const QString& organization, const QString& appName)
{
    QSettings settings(organization, appName);
    settings.beginGroup("TableColumns");
    int columnCount = table->columnCount();
    for (int i = 0; i < columnCount; ++i) {
        settings.setValue(QString("column_%1").arg(i), table->columnWidth(i));
    }
    settings.endGroup();
}

bool loadTableColumnWidthsFromSettings(QTableWidget* table, const QString& organization, const QString& appName)
{
    QSettings settings(organization, appName);
    settings.beginGroup("TableColumns");
    QStringList allKeys = settings.allKeys();
    if (allKeys.isEmpty()) {
        settings.endGroup();
        return false;
    }
    int columnCount = table->columnCount();
    for (int i = 0; i < columnCount; ++i) {
        QString key = QString("column_%1").arg(i);
        if (settings.contains(key)) {
            table->setColumnWidth(i, settings.value(key).toInt());
        }
    }
    settings.endGroup();
    return true;
}

QString convertDateFormat(const QString &inputDate, bool showTime)
{
    if (inputDate.length() < 10 || inputDate[4] != '-' || inputDate[7] != '-') {
        return QString();
    }
    QString year = inputDate.left(4);
    QString month = inputDate.mid(5, 2);
    QString day = inputDate.mid(8, 2);
    QString result = day + "." + month + "." + year;
    if (showTime && inputDate.length() >= 19) {
        QString hours = inputDate.mid(11, 2);
        QString minutes = inputDate.mid(14, 2);
        QString seconds = inputDate.mid(17, 2);
        result += " " + hours + ":" + minutes + ":" + seconds;
    }
    return result;
}

QString convertBackDateFormat(const QString &inputDate)
{
    if (inputDate.length() != 10 || inputDate[2] != '.' || inputDate[5] != '.') {
        return QString();
    }
    QString day = inputDate.left(2);
    QString month = inputDate.mid(3, 2);
    QString year = inputDate.mid(6, 4);
    return year + "-" + month + "-" + day;
}

void resizeColumnsToHeaders(QTableWidget* table)
{
    if (!loadTableColumnWidthsFromSettings(table, "VkPublications", "VkPublicationsApp")) {
        table->ensurePolished();
        table->horizontalHeader()->ensurePolished();
        QFont headerFont = table->horizontalHeader()->font();
        QFontMetrics fm(headerFont);
        int totalMargin = 35;
        for (int col = 0; col < table->columnCount(); ++col) {
            QString headerText = table->horizontalHeaderItem(col)->text();
            int textWidth = fm.horizontalAdvance(headerText);
            int headerWidth = qMax(textWidth + totalMargin, 80);
            table->setColumnWidth(col, headerWidth);
        }
    }
}
