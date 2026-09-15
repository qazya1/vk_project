#ifndef TABLE_UI_UTILS_H
#define TABLE_UI_UTILS_H

#include <QString>
class QTableWidget;

void saveTableColumnWidthsToSettings(const QTableWidget* table, const QString& organization, const QString& appName);
bool loadTableColumnWidthsFromSettings(QTableWidget* table, const QString& organization, const QString& appName);
QString convertDateFormat(const QString &inputDate, bool showTime = false);
QString convertBackDateFormat(const QString &inputDate);
void resizeColumnsToHeaders(QTableWidget* table);

#endif // TABLE_UI_UTILS_H
