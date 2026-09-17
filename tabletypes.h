#pragma once

#include <QMap>
#include <QString>
#include <QVector>
#include <Qt>

struct TableColumnDefinition
{
    QString key;
};

struct TableDefinition
{
    QString id;
    QVector<TableColumnDefinition> columns;
    bool hasSearchRow = false;
    bool paged = true;
    int defaultPageSize = 20;
    int defaultSortColumn = 0;
    Qt::SortOrder defaultSortOrder = Qt::AscendingOrder;
};

struct TableState
{
    int currentPage = 1;
    int pageSize = 20;
    int sortColumn = 0;
    Qt::SortOrder sortOrder = Qt::AscendingOrder;
    bool markersEnabled = false;
    int totalPages = 0;
    int totalRecords = 0;
};


struct TablePaginationUiModel
{
    int currentPage = 1;
    int totalPages = 0;
    int previousPage = 1;
    int nextPage = 1;
    int lastPage = 1;
    int otherPage = 1;
    bool canPrevious = false;
    bool canNext = false;
    bool showOther = false;
    bool showLast = false;
    QVector<int> visiblePages;
};
