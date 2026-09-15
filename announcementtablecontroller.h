#pragma once

#include <QDate>
#include <QStringList>

#include "basetablecontroller.h"
#include "announcementservice.h"

struct AnnouncementTableFilterState
{
    int groupRecordId = 0;
    QDate startDate;
    QDate finishDate;
    QString status;
    bool showMergedVacancy = false;
    bool showMergedAccount = false;
    QStringList allowedFirstWords;
};

class MainWindow;

class AnnouncementTableController : public BaseTableController
{
    Q_OBJECT
public:
    explicit AnnouncementTableController(AnnouncementService *service,
                                         QTableWidget *table,
                                         QObject *parent = nullptr);

    void setup(MainWindow *mainWindow);

    void applyFilterState(const AnnouncementTableFilterState &filterState, bool resetPage = true);
    void syncUiState(const AnnouncementTableFilterState &filterState, int pageSize, bool markersEnabled);

    int groupRecordId() const;
    void reload();
    void reloadPreservingViewState();
    void goToPage(int page);
    void goToOtherPage();
    TablePaginationUiModel paginationUiModel() const;
    bool updateCell(int row, int column);
    void toggleMarkers(bool enabled);
    bool exportCurrent(const QString &basePath) const;
    bool clearAll();

private:
    AnnouncementQueryParams buildQueryParams() const;
    QMap<QString, QString> collectColumnFilters() const;
    QString sortColumnName() const;
    static TableDefinition buildDefinition();
    static QMap<int, QString> dbColumnNames();
    static QMap<int, QString> editableDbColumnNames();

private:
    AnnouncementService *m_service;
    AnnouncementTableFilterState m_filterState;
};
