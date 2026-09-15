#include "announcementtablecontroller.h"

#include <QFile>
#include <QScrollBar>
#include <QItemSelectionModel>
#include <QSignalBlocker>
#include <QTableWidgetItem>

#include "announcementtableformatter.h"
#include "mainwindow.h"
#include "table_ui_utils.h"

AnnouncementTableController::AnnouncementTableController(AnnouncementService *service,
                                                         QTableWidget *table,
                                                         QObject *parent)
    : BaseTableController(buildDefinition(), table, parent)
    , m_service(service)
{
}

void AnnouncementTableController::setup(MainWindow *mainWindow)
{
    if (!m_table) {
        return;
    }

    AnnouncementTableFormatter::setupTable(m_table, mainWindow);
}

void AnnouncementTableController::applyFilterState(const AnnouncementTableFilterState &filterState, bool resetPage)
{
    m_filterState = filterState;
    if (resetPage) {
        m_state.currentPage = 1;
    }
}

void AnnouncementTableController::syncUiState(const AnnouncementTableFilterState &filterState, int pageSize, bool markersEnabled)
{
    m_filterState = filterState;
    setPageSize(pageSize);
    m_state.markersEnabled = markersEnabled;
}

int AnnouncementTableController::groupRecordId() const
{
    return m_filterState.groupRecordId;
}

void AnnouncementTableController::reload()
{
    if (!m_service || !m_table) {
        return;
    }

    const AnnouncementQueryParams params = buildQueryParams();
    int numRecords = 0;
    const QVector<QVector<QVariant> > data = m_service->getPage(params, numRecords, sortColumnName());
    const QSignalBlocker blocker(m_table);
    AnnouncementTableFormatter::populateRows(m_table, data, dbColumnNames());

    m_state.totalRecords = numRecords;
    m_state.totalPages = 0;
    if (m_state.pageSize > 0) {
        m_state.totalPages = numRecords / m_state.pageSize;
        if (numRecords % m_state.pageSize > 0) {
            m_state.totalPages += 1;
        }
    }

    if (m_state.markersEnabled) {
        AnnouncementTableFormatter::toggleMarkers(m_table, true);
    }

    emitStateChanged();
}


void AnnouncementTableController::reloadPreservingViewState()
{
    if (!m_table) {
        reload();
        return;
    }

    const int vScrollValue = m_table->verticalScrollBar() ? m_table->verticalScrollBar()->value() : 0;
    const int hScrollValue = m_table->horizontalScrollBar() ? m_table->horizontalScrollBar()->value() : 0;

    QModelIndexList selectedIndexes;
    if (m_table->selectionModel()) {
        selectedIndexes = m_table->selectionModel()->selectedIndexes();
    }
    int selectedRow = -1;
    int selectedColumn = -1;
    if (!selectedIndexes.isEmpty()) {
        selectedRow = selectedIndexes.first().row();
        selectedColumn = selectedIndexes.first().column();
    }

    reload();

    if (m_table->verticalScrollBar()) {
        m_table->verticalScrollBar()->setValue(vScrollValue);
    }
    if (m_table->horizontalScrollBar()) {
        m_table->horizontalScrollBar()->setValue(hScrollValue);
    }
    if (selectedRow >= 0 && selectedColumn >= 0) {
        m_table->setCurrentCell(selectedRow, selectedColumn);
    }
}

void AnnouncementTableController::goToPage(int page)
{
    setCurrentPage(page);
    reload();
}

void AnnouncementTableController::goToOtherPage()
{
    const TablePaginationUiModel model = paginationUiModel();
    goToPage(model.otherPage);
}

TablePaginationUiModel AnnouncementTableController::paginationUiModel() const
{
    TablePaginationUiModel model;
    model.currentPage = m_state.currentPage;
    model.totalPages = m_state.totalPages;
    model.previousPage = m_state.currentPage > 1 ? (m_state.currentPage - 1) : 1;
    model.nextPage = m_state.currentPage < m_state.totalPages ? (m_state.currentPage + 1) : m_state.totalPages;
    model.lastPage = m_state.totalPages;
    model.canPrevious = m_state.currentPage > 1;
    model.canNext = m_state.currentPage < m_state.totalPages;

    const int buttonsCount = 5;
    if (m_state.totalPages <= 0) {
        model.otherPage = 1;
        return model;
    }

    if (m_state.totalPages <= buttonsCount) {
        for (int i = 1; i <= m_state.totalPages; ++i) {
            model.visiblePages.append(i);
        }
        model.showOther = false;
        model.showLast = false;
        model.otherPage = m_state.totalPages;
        return model;
    }

    int startPage = 1;
    int endPage = m_state.totalPages;
    if (m_state.currentPage > 3) {
        startPage = m_state.currentPage - 2;
        endPage = m_state.currentPage + 2;
        if (endPage > m_state.totalPages) {
            endPage = m_state.totalPages;
            startPage = endPage - 4;
        }
    } else {
        startPage = 1;
        endPage = 5;
    }

    for (int page = startPage; page <= endPage; ++page) {
        model.visiblePages.append(page);
    }

    model.showOther = endPage < m_state.totalPages - 1;
    model.showLast = m_state.currentPage <= m_state.totalPages - 3;
    if (m_state.totalPages - m_state.currentPage > 5) {
        model.otherPage = m_state.currentPage + 5;
    } else {
        model.otherPage = qMax(1, m_state.totalPages - 1);
    }
    return model;
}

bool AnnouncementTableController::updateCell(int row, int column)
{
    if (!m_table || !m_service || row <= 0 || column <= 0) {
        return true;
    }

    const QMap<int, QString> columnsName = editableDbColumnNames();
    const QString columnName = columnsName.value(column);
    if (columnName.isEmpty()) {
        return true;
    }

    QTableWidgetItem *idItem = m_table->item(row, 0);
    QTableWidgetItem *valueItem = m_table->item(row, column);
    if (!idItem || !valueItem) {
        return false;
    }

    const int id = idItem->data(Qt::UserRole).toInt();
    QString value;
    if (valueItem->flags() & Qt::ItemIsEditable) {
        value = valueItem->text();
    } else {
        value = valueItem->data(Qt::UserRole).toString();
    }

    if (columnName == "account_date" || columnName == "publication_date" || columnName == "depublication_date") {
        value = convertBackDateFormat(value);
    }

    return m_service->updateValue(id, columnName, value);
}

void AnnouncementTableController::toggleMarkers(bool enabled)
{
    m_state.markersEnabled = enabled;
    AnnouncementTableFormatter::toggleMarkers(m_table, enabled);
    emitStateChanged();
}

bool AnnouncementTableController::exportCurrent(const QString &basePath) const
{
    if (!m_service || m_filterState.groupRecordId <= 0) {
        return false;
    }
    return m_service->exportGroup(basePath, m_filterState.groupRecordId);
}

bool AnnouncementTableController::clearAll()
{
    if (!m_service) {
        return false;
    }
    const bool ok = m_service->clearAll();
    if (ok) {
        m_state.currentPage = 1;
    }
    return ok;
}

AnnouncementQueryParams AnnouncementTableController::buildQueryParams() const
{
    AnnouncementQueryParams params;
    params.groupId = m_filterState.groupRecordId;
    params.page = m_state.currentPage;
    params.limit = m_state.pageSize;
    params.id = -1;
    params.columnSort = m_state.sortColumn;
    params.sortAsc = (m_state.sortOrder == Qt::AscendingOrder);
    params.dateStart = m_filterState.startDate;
    params.dateFinish = m_filterState.finishDate;
    params.status = m_filterState.status;
    params.showMergedVacancy = m_filterState.showMergedVacancy;
    params.showMergedAccount = m_filterState.showMergedAccount;
    params.columnFilters = collectColumnFilters();
    params.allowedFirstWords = m_filterState.allowedFirstWords;

    QTableWidgetItem *idItem = m_table ? m_table->item(0, 0) : nullptr;
    if (idItem && !idItem->text().isEmpty()) {
        params.id = idItem->text().toInt();
    }

    return params;
}

QMap<QString, QString> AnnouncementTableController::collectColumnFilters() const
{
    QMap<QString, QString> filters;
    if (!m_table) {
        return filters;
    }

    const QMap<int, QString> columnsName = dbColumnNames();
    for (int i = 1; i < m_definition.columns.size(); ++i) {
        QTableWidgetItem *item = m_table->item(0, i);
        if (!item) {
            continue;
        }

        QString value = item->text();
        if (value.isEmpty()) {
            continue;
        }

        const QString key = columnsName.value(i);
        if (key == "account_date" || key == "publication_date" || key == "real_publication_date" || key == "depublication_date") {
            value = convertBackDateFormat(value);
        }
        filters.insert(key, value);
    }
    return filters;
}

QString AnnouncementTableController::sortColumnName() const
{
    return dbColumnNames().value(m_state.sortColumn);
}

TableDefinition AnnouncementTableController::buildDefinition()
{
    TableDefinition def;
    def.id = "announcements";
    def.hasSearchRow = true;
    def.paged = true;
    def.defaultPageSize = 250;
    def.defaultSortColumn = 0;
    def.defaultSortOrder = Qt::AscendingOrder;
    def.columns.resize(23);
    const QMap<int, QString> names = dbColumnNames();
    for (int i = 0; i < def.columns.size(); ++i) {
        def.columns[i].key = names.value(i);
    }
    return def;
}

QMap<int, QString> AnnouncementTableController::dbColumnNames()
{
    QMap<int, QString> columnsName;
    columnsName.insert(0, "id");
    columnsName.insert(1, "vacancy");
    columnsName.insert(2, "status");
    columnsName.insert(3, "vk_single_link");
    columnsName.insert(4, "publication_date");
    columnsName.insert(5, "depublication_date");
    columnsName.insert(6, "account_number");
    columnsName.insert(7, "account_date");
    columnsName.insert(8, "atribute");
    columnsName.insert(9, "inn");
    columnsName.insert(10, "company");
    columnsName.insert(11, "phone");
    columnsName.insert(12, "email");
    columnsName.insert(13, "contact_man");
    columnsName.insert(14, "address");
    columnsName.insert(15, "house_number");
    columnsName.insert(16, "responcibilities");
    columnsName.insert(17, "requirements");
    columnsName.insert(18, "conditions");
    columnsName.insert(19, "work_schedule");
    columnsName.insert(20, "salary");
    columnsName.insert(21, "vk_union_vacancy_link");
    columnsName.insert(22, "vk_union_acc_number_link");
    return columnsName;
}

QMap<int, QString> AnnouncementTableController::editableDbColumnNames()
{
    QMap<int, QString> columnsName = dbColumnNames();
    columnsName.remove(3);
    columnsName.remove(21);
    columnsName.remove(22);
    return columnsName;
}
