#include "groupstabcontroller.h"

#include "ui_mainwindow.h"
#include "adduserdialog.h"
#include "backgroundjobservice.h"
#include "databasemanager.h"
#include "groupservice.h"
#include "groupwidget.h"

#include <QDialog>
#include <QErrorMessage>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

GroupsTabController::GroupsTabController(Ui::MainWindow *ui,
                                         DatabaseManager *databaseManager,
                                         GroupService *groupService,
                                         BackgroundJobService *backgroundJobService,
                                         PublishProcessManager *processManager,
                                         const QString &dbPath,
                                         QWidget *dialogParent,
                                         QObject *parent)
    : QObject(parent)
    , m_ui(ui)
    , m_databaseManager(databaseManager)
    , m_groupService(groupService)
    , m_backgroundJobService(backgroundJobService)
    , m_processManager(processManager)
    , m_dbPath(dbPath)
    , m_dialogParent(dialogParent)
{
}

GroupsTabController::~GroupsTabController()
{
    for (auto it = m_groupWidgets.begin(); it != m_groupWidgets.end(); ++it) {
        delete it.value();
    }
}

void GroupsTabController::setup()
{
    connect(m_ui->addGroupButton, &QPushButton::clicked, this, &GroupsTabController::createNewGroupDialog);
    setupProcessSignals();
}

void GroupsTabController::setupProcessSignals()
{
    connect(m_processManager, &PublishProcessManager::publishingStarted, this, [this](int id, qint64 processId, const QString &message) {
        GroupRuntimeState runtimeState = m_groupService->runtimeState(id);
        runtimeState.processId = processId;
        m_groupService->setRuntimeState(id, runtimeState);
        m_backgroundJobService->startPublishingJob(id, processId, m_processManager->publishingWorkerPath(), m_databaseManager->database());
        refreshWidgets();
        emit announcementReloadRequested(true, false);
        QMessageBox::information(m_dialogParent, QStringLiteral("Скрипт запущен"), message);
    });

    connect(m_processManager, &PublishProcessManager::publishingStartFailed, this, [this](int, const QString &message) {
        QErrorMessage *errorMessage = new QErrorMessage(m_dialogParent);
        errorMessage->showMessage(message);
    });

    connect(m_processManager, &PublishProcessManager::publishingStopped, this, [this](int id, bool success, const QString &message) {
        if (success) {
            GroupRuntimeState runtimeState = m_groupService->runtimeState(id);
            runtimeState.processId = -1;
            m_groupService->setRuntimeState(id, runtimeState);
            m_backgroundJobService->finishPublishingJob(id, QStringLiteral("stopped"), message, m_databaseManager->database());
            refreshWidgets();
            emit announcementReloadRequested(false, false);
            QMessageBox::information(m_dialogParent, QStringLiteral("Скрипт остановлен"), message);
            return;
        }

        QErrorMessage *errorMessage = new QErrorMessage(m_dialogParent);
        errorMessage->showMessage(message);
    });

    connect(m_processManager, &PublishProcessManager::clearWallStarted, this, [this](int, const QString &) {
        refreshWidgets();
    });

    connect(m_processManager, &PublishProcessManager::workerStateChanged, this, [this](int, PublishProcessManager::WorkerState) {
        refreshWidgets();
    });

    connect(m_processManager, &PublishProcessManager::clearWallFinished, this, [this](int id, bool success, const QString &message) {
        if (success) {
            m_groupService->refreshFromStorage(id, *m_databaseManager);
            refreshWidgets();
            QMessageBox::information(m_dialogParent, QStringLiteral("Завершено"), message);
            return;
        }

        QErrorMessage *errorMessage = new QErrorMessage(m_dialogParent);
        errorMessage->showMessage(message);
    });
}

void GroupsTabController::loadInitialGroups()
{
    m_groupService->load(*m_databaseManager);
    restoreDetachedPublishingJobs();

    const QMap<int, Group> loadedGroups = m_groupService->groups();
    for (QMap<int, Group>::const_iterator it = loadedGroups.constBegin(); it != loadedGroups.constEnd(); ++it) {
        createAndBindGroupWidget(it.value());
    }

    m_ui->usersFrame->adjustSize();
    updateGroupComboBox();
}

void GroupsTabController::restoreDetachedPublishingJobs()
{
    const QList<BackgroundJobRecord> jobs = m_backgroundJobService->loadActiveJobs(m_databaseManager->database());
    for (int i = 0; i < jobs.size(); ++i) {
        const BackgroundJobRecord &job = jobs.at(i);
        if (!m_groupService->contains(job.groupRecordId)) {
            continue;
        }

        if (m_processManager->isExternalProcessRunning(job.processId)) {
            GroupRuntimeState runtimeState;
            runtimeState.processId = job.processId;
            m_groupService->setRuntimeState(job.groupRecordId, runtimeState);
            m_processManager->registerDetachedPublishing(job.groupRecordId, job.processId);
        } else {
            GroupRuntimeState runtimeState;
            runtimeState.processId = -1;
            m_groupService->setRuntimeState(job.groupRecordId, runtimeState);
            m_backgroundJobService->finishPublishingJob(job.groupRecordId,
                                                        QStringLiteral("finished"),
                                                        QStringLiteral("Процесс не найден при восстановлении состояния после перезапуска приложения"),
                                                        m_databaseManager->database());
        }
    }
}

void GroupsTabController::createAndBindGroupWidget(const Group &groupData)
{
    QVBoxLayout *groupsLayout = qobject_cast<QVBoxLayout*>(m_ui->usersFrame->layout());
    if (!groupsLayout) {
        return;
    }

    groupwidget *newWidget = new groupwidget(m_ui->usersFrame);
    m_groupWidgets.insert(groupData.id, newWidget);
    groupsLayout->addWidget(newWidget);

    newWidget->applyViewModel(buildGroupWidgetViewModel(groupData));

    connect(newWidget, &groupwidget::collapsedChanged, this, [this](int id, bool collapsed) {
        m_groupsUiState.collapsed[id] = collapsed;
        m_ui->usersFrame->adjustSize();
    });
    connect(newWidget, &groupwidget::deleteRequested, this, &GroupsTabController::deleteGroup);
    connect(newWidget, &groupwidget::editRequested, this, &GroupsTabController::changeGroup);
    connect(newWidget, &groupwidget::publishingToggled, this, &GroupsTabController::startStopPublishing);
    connect(newWidget, &groupwidget::clearWallRequested, this, &GroupsTabController::clearVkWall);
    connect(newWidget, &groupwidget::filterPathEdited, this, &GroupsTabController::onGroupFilterChanged);
}

GroupWidgetViewModel GroupsTabController::buildGroupWidgetViewModel(const Group &groupData) const
{
    GroupWidgetViewModel viewModel;
    viewModel.group = groupData.toDto();
    viewModel.runtimeState = m_groupService->runtimeState(groupData.id);
    viewModel.collapsed = m_groupsUiState.collapsed.value(groupData.id, false);

    const PublishProcessManager::WorkerState workerState = m_processManager->stateForGroup(groupData.id);
    viewModel.publishing = m_processManager->isPublishingActiveForGroup(groupData.id, viewModel.runtimeState);
    viewModel.clearingWall = (workerState == PublishProcessManager::ClearingWall);
    viewModel.stopping = (workerState == PublishProcessManager::Stopping);
    return viewModel;
}

void GroupsTabController::refreshWidgets()
{
    QVBoxLayout *groupsLayout = qobject_cast<QVBoxLayout*>(m_ui->usersFrame->layout());
    if (!groupsLayout) {
        return;
    }

    foreach (int groupId, m_groupWidgets.keys()) {
        groupwidget *groupWidget = m_groupWidgets.value(groupId);
        groupWidget->applyViewModel(buildGroupWidgetViewModel(m_groupService->value(groupId)));
    }

    m_ui->usersFrame->adjustSize();
}

void GroupsTabController::deleteGroup(int id)
{
    if (!m_groupService->remove(id, *m_databaseManager)) {
        QErrorMessage *errorMessage = new QErrorMessage(m_dialogParent);
        errorMessage->showMessage(QStringLiteral("Не удалось удалить сообщество!"));
        return;
    }

    groupwidget *groupWidget = m_groupWidgets.value(id);
    m_groupWidgets.remove(id);
    m_groupsUiState.collapsed.remove(id);

    QVBoxLayout *groupsLayout = qobject_cast<QVBoxLayout*>(m_ui->usersFrame->layout());
    if (groupsLayout) {
        groupsLayout->removeWidget(groupWidget);
    }

    delete groupWidget;

    updateGroupComboBox();
    m_ui->usersFrame->adjustSize();
    emit groupsChanged();
    emit announcementReloadRequested(true, false);
}

void GroupsTabController::createNewGroupDialog()
{
    addUserDialog userDialog(m_dialogParent);
    const int result = userDialog.exec();
    if (result == QDialog::Accepted) {
        if (userDialog.checkInputText()) {
            QString groupLink, refreshToken, clientId, deviceId, groupName;
            userDialog.getInputText(groupLink, groupName, refreshToken, clientId, deviceId);
            Group newGroup(groupLink, groupName, refreshToken, clientId, deviceId);

            if (m_groupService->add(newGroup, *m_databaseManager)) {
                updateGroupComboBox();
                createAndBindGroupWidget(newGroup);
                m_ui->usersFrame->adjustSize();
                emit groupsChanged();
                emit announcementReloadRequested(true, false);
            } else {
                QErrorMessage *errorMessage = new QErrorMessage(m_dialogParent);
                errorMessage->showMessage(QStringLiteral("Не удалось вставить в базу данных!"));
            }
        } else {
            QErrorMessage *errorMessage = new QErrorMessage(m_dialogParent);
            errorMessage->showMessage(QStringLiteral("Поля пусты или некорректно заполнены!"));
        }
    }
}

void GroupsTabController::changeGroup(int id)
{
    addUserDialog userDialog(m_dialogParent);
    const Group oldGroup = m_groupService->value(id);
    userDialog.setText(oldGroup.groupLink, oldGroup.name, oldGroup.refreshToken, oldGroup.clientId, oldGroup.deviceId);
    const int result = userDialog.exec();
    if (result == QDialog::Accepted) {
        if (userDialog.checkInputText()) {
            QString groupLink, refreshToken, clientId, deviceId, groupName;
            userDialog.getInputText(groupLink, groupName, refreshToken, clientId, deviceId);
            Group newGroup(groupLink, groupName, refreshToken, clientId, deviceId);
            newGroup.id = id;
            if (m_groupService->update(newGroup, *m_databaseManager)) {
                updateGroupComboBox();
                m_groupWidgets.value(id)->applyViewModel(buildGroupWidgetViewModel(m_groupService->value(id)));
                emit groupsChanged();
                emit announcementReloadRequested(true, false);
            } else {
                QErrorMessage *errorMessage = new QErrorMessage(m_dialogParent);
                errorMessage->showMessage(QStringLiteral("Не удалось изменить сообщество в базе данных!"));
            }
        } else {
            QErrorMessage *errorMessage = new QErrorMessage(m_dialogParent);
            errorMessage->showMessage(QStringLiteral("Поля пусты или некорректно заполнены!"));
        }
    }
}

void GroupsTabController::startStopPublishing(const PublishJobRequest &request)
{
    const int id = request.group.id;
    const Group currentGroup = m_groupService->value(id);
    m_processManager->togglePublishing(currentGroup.toDto(), m_groupService->runtimeState(id), request, m_dbPath);
}

void GroupsTabController::clearVkWall(int id)
{
    const Group currentGroup = m_groupService->value(id);
    m_processManager->clearWall(currentGroup.toDto(), m_dbPath);
}

void GroupsTabController::updateGroupComboBox()
{
    const int previousGroupId = m_ui->groupSelectWidget->currentData().toInt();
    m_ui->groupSelectWidget->clear();

    const QList<int> ids = m_groupService->sortedGroupIds();
    for (QList<int>::const_iterator it = ids.constBegin(); it != ids.constEnd(); ++it) {
        const Group group = m_groupService->value(*it);
        const QString title = QStringLiteral("Группа ") + QString::number(group.groupId);
        m_ui->groupSelectWidget->addItem(title, *it);
    }

    int groupToSelect = previousGroupId;
    if (groupToSelect <= 0 || !m_groupService->contains(groupToSelect)) {
        groupToSelect = m_ui->groupSelectWidget->count() > 0 ? m_ui->groupSelectWidget->itemData(0).toInt() : 0;
    }

    if (groupToSelect > 0) {
        const int index = m_ui->groupSelectWidget->findData(groupToSelect);
        if (index >= 0) {
            m_ui->groupSelectWidget->setCurrentIndex(index);
        }
    }
}

void GroupsTabController::onGroupFilterChanged(int id, const QString &filterFilePath)
{
    if (!m_groupService->updateFilterPath(id, filterFilePath, *m_databaseManager)) {
        return;
    }

    emit groupFilterChanged(id);
}
