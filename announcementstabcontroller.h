#ifndef ANNOUNCEMENTSTABCONTROLLER_H
#define ANNOUNCEMENTSTABCONTROLLER_H

#include <QObject>
#include <QString>

#include "announcementtablecontroller.h"
#include "group_types.h"

namespace Ui { class MainWindow; }
class AnnouncementService;
class GroupService;
class MainWindow;
class QTimer;
class QWidget;

class AnnouncementsTabController : public QObject
{
    Q_OBJECT
public:
    explicit AnnouncementsTabController(Ui::MainWindow *ui,
                                        AnnouncementService *announcementService,
                                        GroupService *groupService,
                                        MainWindow *mainWindow,
                                        QWidget *dialogParent,
                                        QObject *parent = nullptr);
    ~AnnouncementsTabController();

    void setup();
    void initializeCurrentGroupFromUi();
    void reload(bool resetPage = false, bool preserveViewState = false);
    void restartFilterTimer();
    void stopTimers();
    int currentGroupId() const;

public slots:
    void onGroupsChanged();
    void onGroupFilterChanged(int groupId);

private slots:
    void updatePageButtons(int currentPage = 1);
    void autoRefreshTable();
    void getPage(int page);
    void getPageFromMiddle();
    void tableEdit(int row, int column);
    void tableSort(int index);
    void tableTimeRefresh();
    void unpublishedChoice(bool toggled);
    void activeChoice(bool toggled);
    void finishedChoice(bool toggled);
    void moderatedChoice(bool toggled);
    void textWithMarkers(bool checked);
    void setPageWhenChangeLimit();
    void exportXlsx();
    void clearAll();
    void urlOpenInCell(int row, int column);
    void mergedVacancyChoice(bool checked);
    void mergedAccountChoice(bool checked);
    void groupSelectionChanged(int index);
    void applyFilter();

private:
    AnnouncementTableFilterState currentFilterState() const;
    void setupPageButtonSizes();
    void syncCurrentGroupFromComboBox();

    Ui::MainWindow *m_ui;
    AnnouncementService *m_announcementService;
    GroupService *m_groupService;
    MainWindow *m_mainWindow;
    QWidget *m_dialogParent;
    AnnouncementTableController *m_tableController;
    QTimer *m_refreshTimer;
    QTimer *m_filterTimer;
    TableUiState m_tableUiState;
};

#endif // ANNOUNCEMENTSTABCONTROLLER_H
