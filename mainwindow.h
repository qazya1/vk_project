#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>

#include "databasemanager.h"
#include "announcementservice.h"
#include "xmlimportservice.h"
#include "groupservice.h"
#include "backgroundjobservice.h"
#include "publishprocessmanager.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class AnnouncementsTabController;
class GroupsTabController;
class XmlImportTabController;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void restartFilterTimer();

private slots:
    void changeTabIcons(int tab);

private:
    void setupServices();
    void setupTabs();
    void setupTabIcons();
    void openDatabaseAndLoadInitialData();

    Ui::MainWindow *ui;
    DatabaseManager *databaseManager;
    XmlImportService *xmlImportService;
    AnnouncementService *announcementService;
    GroupService *groupService;
    BackgroundJobService *backgroundJobService;
    PublishProcessManager *processManager;

    AnnouncementsTabController *announcementsTabController;
    XmlImportTabController *xmlImportTabController;
    GroupsTabController *groupsTabController;

    const QString dbPath = "vk_publications.db";
};

#endif // MAINWINDOW_H
