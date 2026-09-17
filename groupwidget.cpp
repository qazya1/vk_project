#include "groupwidget.h"
#include "ui_groupwidget.h"

#include <QAction>
#include <QFileDialog>
#include <QLineEdit>
#include <QSignalBlocker>

void groupwidget::setupClearButton()
{
    QAction *clearAction = new QAction(ui->filterEdit);

    QPixmap pixmap(":/icons/clear_button.png");
    if (!pixmap.isNull()) {
        clearAction->setIcon(QIcon(pixmap));
    }

    ui->filterEdit->addAction(clearAction, QLineEdit::TrailingPosition);
    clearAction->setVisible(false);

    connect(clearAction, &QAction::triggered, ui->filterEdit, &QLineEdit::clear);
    connect(ui->filterEdit, &QLineEdit::textChanged, this, [this, clearAction](const QString &text) {
        clearAction->setVisible(!text.isEmpty());
        if (currentViewModel.group.id > 0) {
            emit filterPathEdited(currentViewModel.group.id, text);
        }
    });
}

groupwidget::groupwidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::groupwidget)
{
    ui->setupUi(this);
    setupClearButton();

    connect(ui->deleteButton, &QPushButton::clicked, this, &groupwidget::handleDelete);
    connect(ui->editButton, &QPushButton::clicked, this, &groupwidget::handleEdit);
    connect(ui->startDeleteButton, &QPushButton::clicked, this, &groupwidget::handleClearWall);
    connect(ui->dialogVacanciesFileButon, &QPushButton::clicked, this, &groupwidget::chooseFilterFile);
    connect(ui->radioButton, &QPushButton::toggled, this, &groupwidget::setCollapsed);
    connect(ui->startParserButton, &QPushButton::clicked, this, &groupwidget::handlePublishToggle);
    connect(ui->everyDayBox, &QPushButton::toggled, this, &groupwidget::handleBlockTime);

    ui->choicePeriodBox->setChecked(true);

    const int height = ui->deleteButton->height();
    foreach(QWidget *widget, ui->frame_6->findChildren<QWidget*>()) {
        widget->setMinimumWidth(height + 2);
        widget->setMaximumWidth(height + 2);
    }

    updateButtons();
}

groupwidget::~groupwidget()
{
    delete ui;
}

void groupwidget::applyViewModel(const GroupWidgetViewModel &viewModel)
{
    currentViewModel = viewModel;

    ui->groupLabel->setText(" Сообщество №" + QString::number(currentViewModel.group.id));
    ui->groupAddressLabel->setText(currentViewModel.group.groupLink);
    ui->refreshTokenLabel->setText(currentViewModel.group.refreshToken);
    ui->clientIDLabel->setText(currentViewModel.group.clientId);
    ui->deviceIDLabel->setText(currentViewModel.group.deviceId);

    {
        QSignalBlocker blocker(ui->filterEdit);
        ui->filterEdit->setText(currentViewModel.group.filterFilePath);
    }

    {
        QSignalBlocker blocker(ui->radioButton);
        ui->radioButton->setChecked(currentViewModel.collapsed);
    }

    ui->frame_2->setVisible(!currentViewModel.collapsed);
    updateButtons();
}

void groupwidget::handleDelete()
{
    emit deleteRequested(currentViewModel.group.id);
}

void groupwidget::handleEdit()
{
    emit editRequested(currentViewModel.group.id);
}

void groupwidget::setCollapsed(bool collapsed)
{
    ui->frame_2->setVisible(!collapsed);
    currentViewModel.collapsed = collapsed;
    emit collapsedChanged(currentViewModel.group.id, collapsed);
}

void groupwidget::handlePublishToggle()
{
    PublishJobRequest request;
    request.group = currentViewModel.group;
    request.runtimeState = currentViewModel.runtimeState;

    request.settings.postInterval = ui->minutesField->value();
    request.settings.startTime = ui->startPeriodEdit->time().toString("HH:mm");
    request.settings.endTime = ui->endPeriodEdit->time().toString("HH:mm");
    request.settings.roundTheClock = ui->everyDayBox->isChecked();
    request.settings.repostEnabled = ui->everyFewDaysBox->isChecked();
    request.settings.repostIntervalDays = ui->repeatPublicationsDayField->value();
    request.settings.mergeVacancies = ui->unionPublicationsByVacancyBox->isChecked();
    request.settings.mergeByNumber = ui->unionPublicationsByNumberBox->isChecked();
    request.settings.hideCompanyNames = ui->dontPublicateCompaniesBox->isChecked();
    request.settings.salaryThreshold = 50000;
    request.settings.hideSalary = ui->salaryMarkBox->isChecked();
    request.settings.vacancyFilter = ui->filterEdit->text();
    request.settings.hideAdditionalInfo = ui->withoutDopInfoBox->isChecked();
    request.settings.hideAddress = ui->withoutAddressBox->isChecked();
    request.settings.deleteAllPosts = ui->delAllPublicationsBox->isChecked();
    request.settings.mergePeriod = ui->unionPublicationsByVacancyEveryFewDaysBox->isChecked();
    request.settings.daysMergePeriod = ui->unionDaysField->value();

    emit publishingToggled(request);
}

void groupwidget::handleBlockTime(bool toggled)
{
    ui->startPeriodEdit->setEnabled(!toggled);
    ui->endPeriodEdit->setEnabled(!toggled);
}

void groupwidget::handleClearWall()
{
    emit clearWallRequested(currentViewModel.group.id);
}

void groupwidget::chooseFilterFile()
{
    const QString filter = "XLSX files (*.xlsx);;TXT files (*.txt);;All files (*.*)";
    const QString fileName = QFileDialog::getOpenFileName(this, QStringLiteral("Выберите файл"), QString(), filter);
    if (!fileName.isEmpty()) {
        ui->filterEdit->setText(fileName);
    }
}

void groupwidget::updateButtons()
{
    if (currentViewModel.stopping) {
        ui->startParserButton->setText(QStringLiteral("Останавливается..."));
    } else if (currentViewModel.publishing) {
        ui->startParserButton->setText(QStringLiteral("Остановить парсинг"));
    } else {
        ui->startParserButton->setText(QStringLiteral("Запустить парсинг"));
    }

    ui->startParserButton->setEnabled(!currentViewModel.clearingWall && !currentViewModel.stopping);

    if (currentViewModel.clearingWall) {
        ui->startDeleteButton->setText(QStringLiteral("Очищается..."));
    } else {
        ui->startDeleteButton->setText(QStringLiteral("Очистить стену"));
    }

    ui->startDeleteButton->setEnabled(!currentViewModel.publishing && !currentViewModel.clearingWall && !currentViewModel.stopping);
}
