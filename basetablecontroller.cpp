#include "basetablecontroller.h"

#include <QtGlobal>

BaseTableController::BaseTableController(const TableDefinition &definition,
                                         QTableWidget *table,
                                         QObject *parent)
    : QObject(parent)
    , m_definition(definition)
    , m_table(table)
{
    m_state.pageSize = m_definition.defaultPageSize;
    m_state.sortColumn = m_definition.defaultSortColumn;
    m_state.sortOrder = m_definition.defaultSortOrder;
}

const TableDefinition &BaseTableController::definition() const
{
    return m_definition;
}

TableState &BaseTableController::state()
{
    return m_state;
}

const TableState &BaseTableController::state() const
{
    return m_state;
}

void BaseTableController::setCurrentPage(int page)
{
    m_state.currentPage = qMax(1, page);
}

void BaseTableController::setPageSize(int pageSize)
{
    const int normalizedPageSize = qMax(1, pageSize);
    if (m_state.pageSize != normalizedPageSize) {
        m_state.pageSize = normalizedPageSize;
        m_state.currentPage = 1;
        return;
    }
    m_state.pageSize = normalizedPageSize;
}

void BaseTableController::setSort(int column, Qt::SortOrder order)
{
    m_state.sortColumn = column;
    m_state.sortOrder = order;
    m_state.currentPage = 1;
}

void BaseTableController::setMarkersEnabled(bool enabled)
{
    m_state.markersEnabled = enabled;
}

void BaseTableController::emitStateChanged()
{
    emit tableStateChanged(m_state);
}
