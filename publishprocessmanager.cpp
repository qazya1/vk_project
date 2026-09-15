#include "publishprocessmanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QDebug>

PublishProcessManager::PublishProcessManager(QObject *parent)
    : QObject(parent)
{
}

PublishProcessManager::~PublishProcessManager()
{
    QHash<int, WorkerRuntimeInfo>::iterator it = workerRuntime.begin();
    for (; it != workerRuntime.end(); ++it) {
        if (it.value().clearWallProcess) {
            it.value().clearWallProcess->disconnect(this);
            it.value().clearWallProcess->deleteLater();
        }
    }
}


void PublishProcessManager::registerDetachedPublishing(int id, qint64 processId)
{
    setState(id, Publishing, processId);
}

bool PublishProcessManager::isExternalProcessRunning(qint64 processId) const
{
    return isOsProcessRunning(processId);
}

QString PublishProcessManager::publishingWorkerPath() const
{
    return workerExecutablePath("./publicate/publicate.exe");
}

QStringList PublishProcessManager::buildPublishArguments(const PublishJobRequest &request, const QString &dbPath) const
{
    const GroupDto group = request.group;
    const PublishSettings settings = request.settings;

    QStringList arguments;
    arguments << QString::number(group.groupId)
              << group.refreshToken
              << group.clientId
              << group.deviceId
              << "--db_path" << dbPath
              << "--post_interval" << QString::number(settings.postInterval)
              << "--start_time" << settings.startTime
              << "--end_time" << settings.endTime;

    if (settings.roundTheClock) arguments << "--round_the_clock";
    if (!settings.repostEnabled) arguments << "--no_repost";
    if (!settings.mergeVacancies) arguments << "--no_merge_vacancies";
    arguments << "--vacancy_filter" << settings.vacancyFilter;
    if (!settings.mergeByNumber) arguments << "--no_merge_by_number";
    if (settings.hideCompanyNames) arguments << "--hide_company_names";
    if (settings.hideAdditionalInfo) arguments << "--hide_additional_info";
    if (settings.hideAddress) arguments << "--hide_address";
    if (settings.deleteAllPosts) arguments << "--delete_all_posts";
    if (settings.hideSalary) arguments << "--hide_salary";

    arguments << "--repost_interval_days" << QString::number(settings.repostIntervalDays)
              << "--salary_threshold" << QString::number(settings.salaryThreshold);

    if (settings.mergePeriod) {
        arguments << "--merge_periodically" << "--merge_interval_days" << QString::number(settings.daysMergePeriod);
    }

    qDebug() << arguments;
    return arguments;
}

QStringList PublishProcessManager::buildClearWallArguments(const GroupDto &group, const QString &dbPath) const
{
    return QStringList()
            << QString::number(group.groupId)
            << group.refreshToken
            << group.clientId
            << group.deviceId
            << "--db_path" << dbPath;
}

QString PublishProcessManager::workerExecutablePath(const QString &relativePath) const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString candidate = QDir(appDir).filePath(relativePath);
    if (QFileInfo::exists(candidate)) {
        return candidate;
    }
    return relativePath;
}

bool PublishProcessManager::isOsProcessRunning(qint64 processId) const
{
    if (processId <= 0) {
        return false;
    }

#ifdef Q_OS_WIN
    QProcess process;
    process.start("cmd", QStringList() << "/C" << "tasklist" << "/FI" << QString("PID eq %1").arg(processId));
    if (!process.waitForFinished(5000)) {
        return false;
    }
    const QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
    return output.contains(QString::number(processId));
#else
    QProcess process;
    process.start("kill", QStringList() << "-0" << QString::number(processId));
    if (!process.waitForFinished(5000)) {
        return false;
    }
    return process.exitCode() == 0;
#endif
}

bool PublishProcessManager::stopOsProcess(qint64 processId, QString *errorMessage) const
{
    if (processId <= 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Некорректный идентификатор процесса");
        }
        return false;
    }

#ifdef Q_OS_WIN
    QProcess process;
    process.start("taskkill", QStringList() << "/F" << "/PID" << QString::number(processId));
    if (!process.waitForFinished(10000)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Не удалось дождаться завершения taskkill");
        }
        return false;
    }

    if (process.exitCode() != 0) {
        if (errorMessage) {
            const QString stdErr = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
            *errorMessage = stdErr.isEmpty()
                    ? QStringLiteral("taskkill завершился с ошибкой")
                    : stdErr;
        }
        return false;
    }
    return true;
#else
    QProcess process;
    process.start("kill", QStringList() << "-TERM" << QString::number(processId));
    if (!process.waitForFinished(10000) || process.exitCode() != 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Не удалось остановить процесс");
        }
        return false;
    }
    return true;
#endif
}

void PublishProcessManager::setState(int id, WorkerState state, qint64 processId)
{
    WorkerRuntimeInfo &info = workerRuntime[id];
    info.state = state;
    if (processId >= 0 || state == Idle) {
        info.processId = (state == Idle) ? -1 : processId;
    }
    emit workerStateChanged(id, state);
}

PublishProcessManager::WorkerRuntimeInfo PublishProcessManager::runtimeInfoFor(int id, const GroupRuntimeState &runtimeState) const
{
    WorkerRuntimeInfo info = workerRuntime.value(id);
    if (info.processId <= 0 && runtimeState.processId > 0) {
        info.processId = runtimeState.processId;
        if (isOsProcessRunning(runtimeState.processId)) {
            info.state = Publishing;
        }
    }
    return info;
}

PublishProcessManager::WorkerState PublishProcessManager::stateForGroup(int id) const
{
    return workerRuntime.value(id).state;
}

bool PublishProcessManager::isGroupBusy(int id) const
{
    const WorkerState state = stateForGroup(id);
    return state == Publishing || state == ClearingWall || state == Stopping;
}

bool PublishProcessManager::isPublishingActiveForGroup(int id, const GroupRuntimeState &runtimeState) const
{
    const WorkerRuntimeInfo info = runtimeInfoFor(id, runtimeState);
    if (info.state == Publishing) {
        return true;
    }
    return info.processId > 0 && isOsProcessRunning(info.processId);
}

bool PublishProcessManager::isClearingWallForGroup(int id) const
{
    return stateForGroup(id) == ClearingWall;
}

void PublishProcessManager::togglePublishing(const GroupDto &group, const GroupRuntimeState &runtimeState, const PublishJobRequest &request, const QString &dbPath)
{
    const WorkerRuntimeInfo info = runtimeInfoFor(group.id, runtimeState);

    if (info.state == ClearingWall) {
        emit publishingStartFailed(group.id, QStringLiteral("Сейчас выполняется очистка стены. Дождитесь завершения операции."));
        return;
    }

    if (isPublishingActiveForGroup(group.id, runtimeState)) {
        setState(group.id, Stopping, info.processId);
        QString errorMessage;
        if (stopOsProcess(info.processId, &errorMessage)) {
            setState(group.id, Idle);
            emit publishingStopped(group.id, true, QStringLiteral("Скрипт остановлен"));
            return;
        }

        setState(group.id, Publishing, info.processId);
        emit publishingStopped(group.id, false, QStringLiteral("Ошибка при остановке скрипта: ") + errorMessage);
        return;
    }

    const QString workerPath = publishingWorkerPath();
    qint64 processId = 0;
    const QStringList arguments = buildPublishArguments(request, dbPath);
    if (!QProcess::startDetached(workerPath, arguments, QString(), &processId)) {
        setState(group.id, Idle);
        emit publishingStartFailed(group.id, QStringLiteral("Не удалось запустить скрипт!"));
        return;
    }

    setState(group.id, Publishing, processId);
    emit publishingStarted(group.id, processId, QStringLiteral("Скрипт запущен"));
}

void PublishProcessManager::clearWall(const GroupDto &group, const QString &dbPath)
{
    const WorkerRuntimeInfo info = runtimeInfoFor(group.id, GroupRuntimeState());
    if (info.state == ClearingWall) {
        emit clearWallFinished(group.id, false, QStringLiteral("Очистка стены уже выполняется для этого сообщества."));
        return;
    }

    if (info.state == Publishing) {
        emit clearWallFinished(group.id, false, QStringLiteral("Сначала остановите скрипт публикации!"));
        return;
    }

    const QString workerPath = workerExecutablePath("./clear_wall/clear_wall.exe");
    QProcess *process = new QProcess(this);
    process->setProgram(workerPath);
    process->setArguments(buildClearWallArguments(group, dbPath));

    WorkerRuntimeInfo &runtime = workerRuntime[group.id];
    runtime.clearWallProcess = process;
    setState(group.id, ClearingWall);
    emit clearWallStarted(group.id, QStringLiteral("Запущена очистка стены"));

    const int id = group.id;

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, id](int exitCode, QProcess::ExitStatus exitStatus) {
        WorkerRuntimeInfo &runtime = workerRuntime[id];
        QPointer<QProcess> finishedProcess = runtime.clearWallProcess;
        runtime.clearWallProcess = nullptr;
        setState(id, Idle);
        if (finishedProcess) {
            finishedProcess->deleteLater();
        }
        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            emit clearWallFinished(id, true, QStringLiteral("Удаление завершено"));
            return;
        }
        emit clearWallFinished(id, false, QStringLiteral("Процесс очистки завершился с ошибкой"));
    });

    connect(process, &QProcess::errorOccurred, this,
            [this, id](QProcess::ProcessError) {
        WorkerRuntimeInfo &runtime = workerRuntime[id];
        QPointer<QProcess> failedProcess = runtime.clearWallProcess;
        const QString errorText = failedProcess ? failedProcess->errorString() : QString();
        runtime.clearWallProcess = nullptr;
        setState(id, Idle);
        if (failedProcess) {
            failedProcess->deleteLater();
        }
        emit clearWallFinished(id, false, QStringLiteral("Ошибка запуска процесса: ") + errorText);
    });

    process->start();
}

