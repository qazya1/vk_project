#pragma once

#include <QObject>
#include <QPointer>
#include <QTableWidget>

#include "tabletypes.h"

class BaseTableController : public QObject
{
    Q_OBJECT
public:
    explicit BaseTableController(const TableDefinition &definition,
                                 QTableWidget *table,
                                 QObject *parent = nullptr);
    virtual ~BaseTableController() {}

    const TableDefinition &definition() const;
    TableState &state();
    const TableState &state() const;

    void setCurrentPage(int page);
    void setPageSize(int pageSize);
    void setSort(int column, Qt::SortOrder order);
    void setMarkersEnabled(bool enabled);
    void clearFilters();

    virtual void reload() = 0;

signals:
    void tableStateChanged(const TableState &state);

protected:
    int columnIndexByKey(const QString &key) const;
    void emitStateChanged();

protected:
    TableDefinition m_definition;
    TableState m_state;
    QPointer<QTableWidget> m_table;
};
