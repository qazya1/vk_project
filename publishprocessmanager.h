#ifndef PUBLISHPROCESSMANAGER_H
#define PUBLISHPROCESSMANAGER_H

#include <QObject>
#include <QHash>
#include <QPointer>
#include <QString>
#include <QStringList>
#include "group_types.h"

class QProcess;

class PublishProcessManager : public QObject
{
    Q_OBJECT
public:
    enum WorkerState {
        Idle,
        Publishing,
        ClearingWall,
        Stopping
    };
    Q_ENUM(WorkerState)

    explicit PublishProcessManager(QObject *parent = nullptr);
    ~PublishProcessManager();

    void togglePublishing(const GroupDto &group, const GroupRuntimeState &runtimeState, const PublishJobRequest &request, const QString &dbPath);
    void clearWall(const GroupDto &group, const QString &dbPath);
    void registerDetachedPublishing(int id, qint64 processId);
    bool isExternalProcessRunning(qint64 processId) const;
    QString publishingWorkerPath() const;

    WorkerState stateForGroup(int id) const;
    bool isGroupBusy(int id) const;
    bool isPublishingActiveForGroup(int id, const GroupRuntimeState &runtimeState) const;
    bool isClearingWallForGroup(int id) const;

signals:
    void workerStateChanged(int id, PublishProcessManager::WorkerState state);
    void publishingStarted(int id, qint64 processId, const QString &message);
    void publishingStartFailed(int id, const QString &message);
    void publishingStopped(int id, bool success, const QString &message);
    void clearWallStarted(int id, const QString &message);
    void clearWallFinished(int id, bool success, const QString &message);

private:
    struct WorkerRuntimeInfo {
        WorkerState state = Idle;
        qint64 processId = -1;
        QPointer<QProcess> clearWallProcess;
    };

    QStringList buildPublishArguments(const PublishJobRequest &request, const QString &dbPath) const;
    QStringList buildClearWallArguments(const GroupDto &group, const QString &dbPath) const;
    bool isOsProcessRunning(qint64 processId) const;
    bool stopOsProcess(qint64 processId, QString *errorMessage = nullptr) const;
    void setState(int id, WorkerState state, qint64 processId = -1);
    WorkerRuntimeInfo runtimeInfoFor(int id, const GroupRuntimeState &runtimeState) const;
    QString workerExecutablePath(const QString &relativePath) const;

    QHash<int, WorkerRuntimeInfo> workerRuntime;
};

#endif // PUBLISHPROCESSMANAGER_H
