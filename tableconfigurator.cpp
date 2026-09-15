#include "tableconfigurator.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QLineEdit>
#include <QPixmap>
#include <QScrollBar>
#include <QTableWidget>
#include <QTableWidgetItem>

#include "mainwindow.h"
#include "tabledelegates.h"

void TableConfigurator::apply(QTableWidget *table,
                              const TableDefinition &definition,
                              MainWindow *mainWindow)
{
    if (!table) {
        return;
    }

    const int columnCount = definition.columns.size();
    if (columnCount > 0) {
        table->setColumnCount(columnCount);
        for (int i = 0; i < columnCount; ++i) {
            const ColumnDefinition &column = definition.columns[i];
            if (!column.headerText.isNull()) {
                QTableWidgetItem *headerItem = table->horizontalHeaderItem(i);
                if (!headerItem) {
                    headerItem = new QTableWidgetItem();
                    table->setHorizontalHeaderItem(i, headerItem);
                }
                headerItem->setText(column.headerText);
            }
            if (column.width > 0) {
                table->setColumnWidth(i, column.width);
            }
        }
    }

    table->horizontalHeader()->setMinimumHeight(definition.headerMinimumHeight);

    CommonDelegate *commonDelegate = new CommonDelegate(table->viewport());
    commonDelegate->setHoverCursorColumns(definition.hover.columns);
    commonDelegate->setHoverCursor(definition.hover.cursorShape);
    table->setItemDelegate(commonDelegate);
    table->setMouseTracking(true);
    table->viewport()->setMouseTracking(true);

    if (definition.searchRow.enabled && definition.searchRow.row >= 0) {
        table->setItemDelegateForRow(definition.searchRow.row, new SearchDelegate(mainWindow, table));
    }

    for (int i = 0; i < definition.columns.size(); ++i) {
        if (definition.columns[i].delegateKind == HtmlDelegateKind) {
            table->setItemDelegateForColumn(i, new HTMLDelegate(table));
        }
    }

    if (definition.searchRow.enabled && definition.searchRow.row >= 0) {
        if (table->rowCount() <= definition.searchRow.row) {
            table->setRowCount(definition.searchRow.row + 1);
        }
        createSearchRow(table, definition.searchRow.row, qMax(columnCount, table->columnCount()), ":/icons/search_icon.png");
        table->setRowHeight(definition.searchRow.row, definition.searchRowHeight);
    }

    table->setIconSize(definition.iconSize);
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    QScrollBar *vScrollBar = table->verticalScrollBar();
    vScrollBar->setSingleStep(definition.scrollSingleStep);
    vScrollBar->setPageStep(definition.scrollPageStep);
}

void TableConfigurator::createSearchRow(QTableWidget *table,
                                        int row,
                                        int columnCount,
                                        const QString &iconPath)
{
    if (!table || row < 0 || columnCount <= 0) {
        return;
    }

    QFont font;
    font.setFamily("Roboto Light");
    font.setPixelSize(20);

    QIcon searchIcon;
    const QPixmap scaledSearch = QPixmap(iconPath).scaled(22, 23, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    searchIcon.addPixmap(scaledSearch, QIcon::Normal);
    searchIcon.addPixmap(scaledSearch, QIcon::Active);
    searchIcon.addPixmap(scaledSearch, QIcon::Selected);

    for (int columnIndex = 0; columnIndex < columnCount; ++columnIndex) {
        QTableWidgetItem *item = table->item(row, columnIndex);
        if (!item) {
            item = new QTableWidgetItem();
            table->setItem(row, columnIndex, item);
        }
        item->setFont(font);
        item->setForeground(QBrush(QColor("#62560E")));
        item->setIcon(searchIcon);
    }
}

void TableConfigurator::toggleHtmlDisplayForColumns(QTableWidget *table,
                                                    bool enabled,
                                                    const HtmlToggleConfig &config)
{
    if (!table) {
        return;
    }

    if (enabled) {
        for (int idx = 0; idx < config.columns.size(); ++idx) {
            const int column = config.columns[idx];
            table->setItemDelegateForColumn(column, new HTMLDelegate(table));

            QVector<QString> texts;
            for (int row = 1; row < table->rowCount(); ++row) {
                QTableWidgetItem *item = table->item(row, column);
                if (!item) {
                    continue;
                }
                texts.push_back(item->text());
                item->setText("");
            }

            const QString style = QString("font-size: %1px;font-family: \"%2\";color: %3;")
                    .arg(config.fontPixelSize)
                    .arg(config.fontFamily)
                    .arg(config.textColor);

            for (int row = 1; row < table->rowCount(); ++row) {
                QTableWidgetItem *item = table->item(row, column);
                if (!item) {
                    continue;
                }
                QString htmlText = texts.value(row - 1);
                if (htmlText.left(5).toUpper() == "<BR/>") {
                    htmlText = htmlText.right(htmlText.length() - 5);
                }
                htmlText = QString("<div style='%1'>%2</div>").arg(style).arg(htmlText);
                item->setData(Qt::DisplayRole, htmlText);
                item->setData(Qt::UserRole, texts.value(row - 1));
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            }
        }
        table->resizeRowsToContents();
        return;
    }

    for (int idx = 0; idx < config.columns.size(); ++idx) {
        const int column = config.columns[idx];
        table->setItemDelegateForColumn(column, nullptr);
        for (int row = 1; row < table->rowCount(); ++row) {
            QTableWidgetItem *item = table->item(row, column);
            if (!item) {
                continue;
            }
            item->setText(item->data(Qt::UserRole).toString());
            item->setFlags(item->flags() | Qt::ItemIsEditable);
        }
    }

    for (int row = 1; row < table->rowCount(); ++row) {
        table->setRowHeight(row, config.defaultRowHeight);
    }
}
