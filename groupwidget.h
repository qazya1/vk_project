#ifndef GROUPWIDGET_H
#define GROUPWIDGET_H

#include <QWidget>
#include "group_types.h"

namespace Ui {
class groupwidget;
}

class groupwidget : public QWidget
{
    Q_OBJECT
signals:
    void deleteRequested(int id);
    void editRequested(int id);
    void collapsedChanged(int id, bool collapsed);
    void clearWallRequested(int id);
    void publishingToggled(const PublishJobRequest &request);
    void filterPathEdited(int id, const QString &filterFilePath);

public:
    explicit groupwidget(QWidget *parent = nullptr);
    ~groupwidget();

    void applyViewModel(const GroupWidgetViewModel &viewModel);

private:
    Ui::groupwidget *ui;
    GroupWidgetViewModel currentViewModel;

    void handleDelete();
    void handleEdit();
    void chooseFilterFile();
    void setCollapsed(bool collapsed);
    void handlePublishToggle();
    void handleBlockTime(bool toggled);
    void handleClearWall();
    void setupClearButton();
    void updateButtons();
};

#endif // GROUPWIDGET_H
