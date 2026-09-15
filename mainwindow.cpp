#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "announcementstabcontroller.h"
#include "groupstabcontroller.h"
#include "xmlimporttabcontroller.h"

#include <QErrorMessage>
#include <QIcon>
#include <QMetaType>
#include <QMessageBox>
#include <QSize>
#include <QTabWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , databaseManager(nullptr)
    , xmlImportService(nullptr)
    , announcementService(nullptr)
    , groupService(nullptr)
    , backgroundJobService(nullptr)
    , processManager(nullptr)
    , announcementsTabController(nullptr)
    , xmlImportTabController(nullptr)
    , groupsTabController(nullptr)
{
    ui->setupUi(this);

    qRegisterMetaType<GroupDto>("GroupDto");
    qRegisterMetaType<GroupRuntimeState>("GroupRuntimeState");
    qRegisterMetaType<PublishSettings>("PublishSettings");
    qRegisterMetaType<GroupWidgetViewModel>("GroupWidgetViewModel");
    qRegisterMetaType<PublishJobRequest>("PublishJobRequest");
    qRegisterMetaType<BackgroundJobRecord>("BackgroundJobRecord");

    setupServices();
    setupTabIcons();
    setupTabs();
    openDatabaseAndLoadInitialData();

    showMaximized();
}

MainWindow::~MainWindow()
{
    delete announcementsTabController;
    delete xmlImportTabController;
    delete groupsTabController;
    delete ui;
    delete announcementService;
    delete xmlImportService;
    delete groupService;
    delete backgroundJobService;
    delete databaseManager;
}

void MainWindow::setupServices()
{
    databaseManager = new DatabaseManager();
    xmlImportService = new XmlImportService(*databaseManager);
    announcementService = new AnnouncementService(*databaseManager, *xmlImportService);
    groupService = new GroupService();
    backgroundJobService = new BackgroundJobService();
    processManager = new PublishProcessManager(this);
}

void MainWindow::setupTabIcons()
{
    ui->tabWidget->setIconSize(QSize(35, 40));
    ui->tabWidget->setTabIcon(0, QIcon(":/icons/publications_icon_red.png"));
    ui->tabWidget->setTabIcon(1, QIcon(":/icons/xml_icon.png"));
    ui->tabWidget->setTabIcon(2, QIcon(":/icons/groups_icon.png"));

    connect(ui->tabWidget, &QTabWidget::currentChanged, this, &MainWindow::changeTabIcons);
}

void MainWindow::setupTabs()
{
    announcementsTabController = new AnnouncementsTabController(ui,
                                                                announcementService,
                                                                groupService,
                                                                this,
                                                                this,
                                                                this);
    xmlImportTabController = new XmlImportTabController(ui,
                                                        announcementService,
                                                        this,
                                                        this);
    groupsTabController = new GroupsTabController(ui,
                                                  databaseManager,
                                                  groupService,
                                                  backgroundJobService,
                                                  processManager,
                                                  dbPath,
                                                  this,
                                                  this);

    announcementsTabController->setup();
    xmlImportTabController->setup();
    groupsTabController->setup();

    connect(xmlImportTabController, &XmlImportTabController::xmlImportedSuccessfully,
            announcementsTabController, [this]() { announcementsTabController->reload(); });

    connect(groupsTabController, &GroupsTabController::groupsChanged,
            announcementsTabController, &AnnouncementsTabController::onGroupsChanged);

    connect(groupsTabController, &GroupsTabController::groupFilterChanged,
            announcementsTabController, &AnnouncementsTabController::onGroupFilterChanged);

    connect(groupsTabController, &GroupsTabController::announcementReloadRequested,
            announcementsTabController, &AnnouncementsTabController::reload);
}

void MainWindow::openDatabaseAndLoadInitialData()
{
    if (!databaseManager->open(dbPath)) {
        QErrorMessage *errorMessage = new QErrorMessage(this);
        errorMessage->showMessage(QStringLiteral("Не удалось подключиться к базе данных!"));
        return;
    }

    groupsTabController->loadInitialGroups();
    announcementsTabController->initializeCurrentGroupFromUi();
    announcementsTabController->reload();
}

void MainWindow::restartFilterTimer()
{
    if (announcementsTabController) {
        announcementsTabController->restartFilterTimer();
    }
}

void MainWindow::changeTabIcons(int tab)
{
    switch (tab) {
    case 0:
        ui->tabWidget->setTabIcon(0, QIcon(":/icons/publications_icon_red.png"));
        ui->tabWidget->setTabIcon(1, QIcon(":/icons/xml_icon.png"));
        ui->tabWidget->setTabIcon(2, QIcon(":/icons/groups_icon.png"));
        break;
    case 1:
        ui->tabWidget->setTabIcon(0, QIcon(":/icons/publications_icon.png"));
        ui->tabWidget->setTabIcon(1, QIcon(":/icons/xml_icon_red.png"));
        ui->tabWidget->setTabIcon(2, QIcon(":/icons/groups_icon.png"));
        break;
    case 2:
        ui->tabWidget->setTabIcon(0, QIcon(":/icons/publications_icon.png"));
        ui->tabWidget->setTabIcon(1, QIcon(":/icons/xml_icon.png"));
        ui->tabWidget->setTabIcon(2, QIcon(":/icons/groups_icon_red.png"));
        break;
    }
}
