#ifndef TABLECONFIGURATOR_H
#define TABLECONFIGURATOR_H

#include <QSize>
#include <QString>
#include <QVector>

class MainWindow;
class QTableWidget;

class TableConfigurator
{
public:
    enum DelegateKind {
        NoDelegate,
        HtmlDelegateKind
    };

    struct ColumnDefinition {
        QString headerText;
        DelegateKind delegateKind = NoDelegate;
        int width = -1;
    };

    struct SearchRowDefinition {
        int row = -1;
        bool enabled = false;
    };

    struct HoverConfig {
        QVector<int> columns;
        Qt::CursorShape cursorShape = Qt::PointingHandCursor;
    };

    struct HtmlToggleConfig {
        QVector<int> columns;
        QString textColor, fontFamily;
        HtmlToggleConfig()
            : textColor("#62560E")
            , fontFamily("Roboto Light")
        {
        }
        int fontPixelSize = 20;
        int defaultRowHeight = 71;
    };

    struct TableDefinition {
        QVector<ColumnDefinition> columns;
        SearchRowDefinition searchRow;
        HoverConfig hover;
        int headerMinimumHeight = 97;
        int searchRowHeight = 50;
        QSize iconSize = QSize(22, 23);
        int scrollSingleStep = 10;
        int scrollPageStep = 100;
    };

    static void apply(QTableWidget *table,
                      const TableDefinition &definition,
                      MainWindow *mainWindow);
    static void createSearchRow(QTableWidget *table,
                                int row,
                                int columnCount,
                                const QString &iconPath);
    static void toggleHtmlDisplayForColumns(QTableWidget *table,
                                            bool enabled,
                                            const HtmlToggleConfig &config = HtmlToggleConfig());
};

#endif // TABLECONFIGURATOR_H
