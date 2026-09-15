#ifndef GROUPSTABCONTROLLER_H
#define GROUPSTABCONTROLLER_H

#include <QObject>
#include <QMap>
#include <QString>

#include "group.h"
#include "group_types.h"
#include "publishprocessmanager.h"

namespace Ui { class MainWindow; }
class BackgroundJobService;
class DatabaseManager;
class GroupService;
class QWidget;
class groupwidget;

class GroupsTabController : public QObject
{
    Q_OBJECT
public:
    explicit GroupsTabController(Ui::MainWindow *ui,
                                 DatabaseManager *databaseManager,
                                 GroupService *groupService,
                                 BackgroundJobService *backgroundJobService,
                                 PublishProcessManager *processManager,
                                 const QString &dbPath,
                                 QWidget *dialogParent,
                                 QObject *parent = nullptr);
    ~GroupsTabController();

    void setup();
    void loadInitialGroups();
    void refreshWidgets();
    void updateGroupComboBox();

signals:
    void groupsChanged();
    void groupFilterChanged(int groupId);
    void announcementReloadRequested(bool resetPage, bool preserveViewState);

private slots:
    void createNewGroupDialog();
    void deleteGroup(int id);
    void changeGroup(int id);
    void startStopPublishing(const PublishJobRequest &request);
    void clearVkWall(int id);
    void onGroupFilterChanged(int id, const QString &filterFilePath);

private:
    void setupProcessSignals();
    void restoreDetachedPublishingJobs();
    void createAndBindGroupWidget(const Group &groupData);
    GroupWidgetViewModel buildGroupWidgetViewModel(const Group &groupData) const;

    Ui::MainWindow *m_ui;
    DatabaseManager *m_databaseManager;
    GroupService *m_groupService;
    BackgroundJobService *m_backgroundJobService;
    PublishProcessManager *m_processManager;
    QString m_dbPath;
    QWidget *m_dialogParent;
    QMap<int, groupwidget*> m_groupWidgets;
    GroupsUiState m_groupsUiState;
};

#endif // GROUPSTABCONTROLLER_H
