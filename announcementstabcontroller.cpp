#include "announcementstabcontroller.h"

#include "ui_mainwindow.h"
#include "announcementservice.h"
#include "groupservice.h"
#include "mainwindow.h"
#include "pagebuttonclass.h"
#include "table_ui_utils.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QDesktopServices>
#include <QErrorMessage>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QUrl>
#include <QWidget>

AnnouncementsTabController::AnnouncementsTabController(Ui::MainWindow *ui,
                                                       AnnouncementService *announcementService,
                                                       GroupService *groupService,
                                                       MainWindow *mainWindow,
                                                       QWidget *dialogParent,
                                                       QObject *parent)
    : QObject(parent)
    , m_ui(ui)
    , m_announcementService(announcementService)
    , m_groupService(groupService)
    , m_mainWindow(mainWindow)
    , m_dialogParent(dialogParent)
    , m_tableController(new AnnouncementTableController(announcementService, ui->tableWidget, this))
    , m_refreshTimer(nullptr)
    , m_filterTimer(nullptr)
{
}

AnnouncementsTabController::~AnnouncementsTabController()
{
    stopTimers();
    saveTableColumnWidthsToSettings(m_ui->tableWidget, "VkPublications", "VkPublicationsApp");
}

void AnnouncementsTabController::setup()
{
    m_ui->rowsNumberWidget->addItems({"250", "500", "1000"});

    const QDate currentDate = QDate::currentDate();
    m_ui->dateStartWidget->setDate(currentDate.addDays(-15));
    m_ui->dateFinishWidget->setDate(currentDate);
    m_ui->dateFinishWidget->setMinimumDate(m_ui->dateStartWidget->date());

    connect(m_ui->dateStartWidget, &QDateEdit::dateChanged, this, &AnnouncementsTabController::tableTimeRefresh);
    connect(m_ui->dateFinishWidget, &QDateEdit::dateChanged, this, &AnnouncementsTabController::tableTimeRefresh);

    QHeaderView *horizontalHeader = m_ui->tableWidget->horizontalHeader();
    connect(horizontalHeader, &QHeaderView::sectionClicked, this, &AnnouncementsTabController::tableSort);
    m_tableUiState.tableHeadersSortAsc = true;
    m_tableUiState.lastTableHeaderSort = 0;

    connect(m_ui->radioButtonNonPublished, &QRadioButton::toggled, this, &AnnouncementsTabController::unpublishedChoice);
    connect(m_ui->radioButtonActive, &QRadioButton::toggled, this, &AnnouncementsTabController::activeChoice);
    connect(m_ui->radioButtonFinished, &QRadioButton::toggled, this, &AnnouncementsTabController::finishedChoice);
    connect(m_ui->radioButtonModerated, &QRadioButton::toggled, this, &AnnouncementsTabController::moderatedChoice);
    connect(m_ui->checkBoxMergedVacancy, &QCheckBox::toggled, this, &AnnouncementsTabController::mergedVacancyChoice);
    connect(m_ui->checkBoxMergedAccount, &QCheckBox::toggled, this, &AnnouncementsTabController::mergedAccountChoice);

    connect(m_ui->checkBoxMarkers, &QCheckBox::clicked, this, [this](bool checked) {
        m_tableUiState.markersEnabled = checked;
        textWithMarkers(checked);
    });

    connect(m_ui->rowsNumberWidget, &QComboBox::currentTextChanged, this, &AnnouncementsTabController::setPageWhenChangeLimit);
    connect(m_ui->exportButton, &QPushButton::clicked, this, &AnnouncementsTabController::exportXlsx);
    connect(m_ui->delButton, &QPushButton::clicked, this, &AnnouncementsTabController::clearAll);

    connect(m_ui->groupSelectWidget, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AnnouncementsTabController::groupSelectionChanged);

    m_tableUiState = TableUiState();
    m_tableUiState.markersEnabled = m_ui->checkBoxMarkers->isChecked();

    m_tableController->setup(m_mainWindow);
    resizeColumnsToHeaders(m_ui->tableWidget);
    connect(m_tableController, &AnnouncementTableController::tableStateChanged, this, [this](const TableState &state) {
        m_tableUiState.totalPages = state.totalPages;
        m_tableUiState.currentPage = state.currentPage;
        m_tableUiState.currentColumnSort = state.sortColumn;
        m_tableUiState.currentSortAsc = (state.sortOrder == Qt::AscendingOrder);
        m_tableUiState.markersEnabled = state.markersEnabled;
        m_ui->applicantsNumberWidget->setValue(state.totalRecords);
        updatePageButtons();
    });

    connect(m_ui->tableWidget, &QTableWidget::cellClicked, this, &AnnouncementsTabController::urlOpenInCell);

    setupPageButtonSizes();
    m_ui->tableWidget->setEditTriggers(QAbstractItemView::AllEditTriggers);

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, &AnnouncementsTabController::autoRefreshTable);
    m_refreshTimer->start(30000);

    m_filterTimer = new QTimer(this);
    m_filterTimer->setSingleShot(true);
    m_filterTimer->setInterval(300);
    connect(m_filterTimer, &QTimer::timeout, this, &AnnouncementsTabController::applyFilter);
}

void AnnouncementsTabController::initializeCurrentGroupFromUi()
{
    syncCurrentGroupFromComboBox();
}

void AnnouncementsTabController::syncCurrentGroupFromComboBox()
{
    int selectedGroupId = 0;
    if (m_ui->groupSelectWidget->count() > 0) {
        selectedGroupId = m_ui->groupSelectWidget->currentData().toInt();
    }

    if (selectedGroupId > 0 && m_groupService->contains(selectedGroupId)) {
        m_tableUiState.currentGroupId = selectedGroupId;
        return;
    }

    m_tableUiState.currentGroupId = 0;
}

void AnnouncementsTabController::stopTimers()
{
    if (m_refreshTimer && m_refreshTimer->isActive()) {
        m_refreshTimer->stop();
    }
    if (m_filterTimer && m_filterTimer->isActive()) {
        m_filterTimer->stop();
    }
}

int AnnouncementsTabController::currentGroupId() const
{
    return m_tableUiState.currentGroupId;
}

void AnnouncementsTabController::onGroupsChanged()
{
    syncCurrentGroupFromComboBox();
    reload(true);
}

void AnnouncementsTabController::onGroupFilterChanged(int groupId)
{
    if (groupId == m_tableUiState.currentGroupId) {
        restartFilterTimer();
    }
}

AnnouncementTableFilterState AnnouncementsTabController::currentFilterState() const
{
    QString status;
    if (m_ui->radioButtonNonPublished->isChecked()) status = QStringLiteral("Не опубликованное");
    else if (m_ui->radioButtonActive->isChecked()) status = QStringLiteral("Активное");
    else if (m_ui->radioButtonFinished->isChecked()) status = QStringLiteral("Завершённое");
    else if (m_ui->radioButtonModerated->isChecked()) status = QStringLiteral("На модерации");

    QStringList allowedFirstWords;
    if (m_tableUiState.currentGroupId > 0 && m_groupService->contains(m_tableUiState.currentGroupId)) {
        const Group currentGroup = m_groupService->value(m_tableUiState.currentGroupId);
        if (!currentGroup.filterFilePath.isEmpty() && QFile::exists(currentGroup.filterFilePath)) {
            allowedFirstWords = m_announcementService->loadFirstWordsFromFile(currentGroup.filterFilePath);
        }
    }

    AnnouncementTableFilterState filterState;
    filterState.groupRecordId = m_tableUiState.currentGroupId;
    filterState.startDate = m_ui->dateStartWidget->date();
    filterState.finishDate = m_ui->dateFinishWidget->date();
    filterState.status = status;
    filterState.showMergedVacancy = m_ui->checkBoxMergedVacancy->isChecked();
    filterState.showMergedAccount = m_ui->checkBoxMergedAccount->isChecked();
    filterState.allowedFirstWords = allowedFirstWords;
    return filterState;
}

void AnnouncementsTabController::reload(bool resetPage, bool preserveViewState)
{
    m_tableController->applyFilterState(currentFilterState(), resetPage);
    m_tableController->setSort(m_tableUiState.currentColumnSort,
                               m_tableUiState.currentSortAsc ? Qt::AscendingOrder : Qt::DescendingOrder);
    m_tableController->setPageSize(m_ui->rowsNumberWidget->currentText().toInt());

    disconnect(m_ui->tableWidget, &QTableWidget::cellChanged, this, &AnnouncementsTabController::tableEdit);
    m_tableController->setMarkersEnabled(m_ui->checkBoxMarkers->isChecked());
    if (preserveViewState) {
        m_tableController->reloadPreservingViewState();
    } else {
        m_tableController->reload();
    }
    connect(m_ui->tableWidget, &QTableWidget::cellChanged, this, &AnnouncementsTabController::tableEdit);
}

void AnnouncementsTabController::getPage(int page)
{
    m_tableController->goToPage(page);
}

void AnnouncementsTabController::getPageFromMiddle()
{
    m_tableController->goToOtherPage();
}

void AnnouncementsTabController::tableEdit(int row, int column)
{
    if (row == 0) {
        return;
    }

    if (!m_tableController->updateCell(row, column)) {
        QErrorMessage *errorMessage = new QErrorMessage(m_dialogParent);
        errorMessage->showMessage(QStringLiteral("Не удалось изменить данные!"));
    }
}

void AnnouncementsTabController::tableSort(int index)
{
    if (index == m_tableUiState.lastTableHeaderSort) {
        m_tableUiState.tableHeadersSortAsc = !m_tableUiState.tableHeadersSortAsc;
    }
    m_tableUiState.lastTableHeaderSort = index;
    m_tableUiState.currentColumnSort = index;
    m_tableUiState.currentSortAsc = m_tableUiState.tableHeadersSortAsc;
    m_tableUiState.currentPage = 1;
    reload(true);
}

void AnnouncementsTabController::groupSelectionChanged(int index)
{
    Q_UNUSED(index);
    const int previousGroupId = m_tableUiState.currentGroupId;
    syncCurrentGroupFromComboBox();
    if (previousGroupId != m_tableUiState.currentGroupId) {
        reload(true);
    }
}

void AnnouncementsTabController::tableTimeRefresh()
{
    m_ui->dateFinishWidget->setMinimumDate(m_ui->dateStartWidget->date());
    if (m_ui->dateStartWidget->date() > m_ui->dateFinishWidget->date()) {
        m_ui->dateFinishWidget->setDate(m_ui->dateStartWidget->date());
    }
    reload(true);
}

void AnnouncementsTabController::unpublishedChoice(bool toggled)
{
    if (toggled) {
        m_ui->radioButtonActive->setChecked(false);
        m_ui->radioButtonFinished->setChecked(false);
        m_ui->radioButtonModerated->setChecked(false);
    }
    reload();
}

void AnnouncementsTabController::activeChoice(bool toggled)
{
    if (toggled) {
        m_ui->radioButtonNonPublished->setChecked(false);
        m_ui->radioButtonFinished->setChecked(false);
        m_ui->radioButtonModerated->setChecked(false);
    }
    reload();
}

void AnnouncementsTabController::finishedChoice(bool toggled)
{
    if (toggled) {
        m_ui->radioButtonNonPublished->setChecked(false);
        m_ui->radioButtonActive->setChecked(false);
        m_ui->radioButtonModerated->setChecked(false);
    }
    reload();
}

void AnnouncementsTabController::moderatedChoice(bool toggled)
{
    if (toggled) {
        m_ui->radioButtonNonPublished->setChecked(false);
        m_ui->radioButtonActive->setChecked(false);
        m_ui->radioButtonFinished->setChecked(false);
    }
    reload();
}

void AnnouncementsTabController::textWithMarkers(bool checked)
{
    m_tableController->toggleMarkers(checked);
}

void AnnouncementsTabController::setPageWhenChangeLimit()
{
    reload(true);
}

void AnnouncementsTabController::exportXlsx()
{
    if (m_tableUiState.currentGroupId <= 0) {
        QErrorMessage *errorMessage = new QErrorMessage(m_dialogParent);
        errorMessage->showMessage(QStringLiteral("Выберите группу для экспорта!"));
        return;
    }

    const QString basePath = QFileDialog::getExistingDirectory(m_dialogParent, QStringLiteral("Выберите папку для сохранения"));
    if (basePath.isEmpty()) {
        return;
    }

    if (m_tableController->exportCurrent(basePath)) {
        QMessageBox::information(m_dialogParent, QStringLiteral("Экспорт завершён"),
                                 QStringLiteral("Экспорт данных для группы %1 завершён").arg(m_tableUiState.currentGroupId));
    } else {
        QErrorMessage *errorMessage = new QErrorMessage(m_dialogParent);
        errorMessage->showMessage(QStringLiteral("Не удалось экспортировать данные!"));
    }
}

void AnnouncementsTabController::clearAll()
{
    if (m_tableController->clearAll()) {
        QMessageBox::information(m_dialogParent, QStringLiteral("Данные удалены"), QStringLiteral("Данные удалены"));
    } else {
        QErrorMessage *errorMessage = new QErrorMessage(m_dialogParent);
        errorMessage->showMessage(QStringLiteral("Не удалось удалить данные!"));
    }
    reload();
}

void AnnouncementsTabController::urlOpenInCell(int row, int column)
{
    if ((column == 3 || column > 21) && row > 0) {
        QTableWidgetItem *item = m_ui->tableWidget->item(row, column);
        if (item) {
            const QString url = item->text();
            if (!url.isEmpty()) {
                QDesktopServices::openUrl(QUrl(url));
            }
        }
    }
}

void AnnouncementsTabController::mergedVacancyChoice(bool)
{
    reload();
}

void AnnouncementsTabController::mergedAccountChoice(bool)
{
    reload();
}

void AnnouncementsTabController::autoRefreshTable()
{
    reload(false, true);
}

void AnnouncementsTabController::restartFilterTimer()
{
    if (m_filterTimer && m_filterTimer->isActive()) {
        m_filterTimer->stop();
    }
    if (m_filterTimer) {
        m_filterTimer->start();
    }
}

void AnnouncementsTabController::applyFilter()
{
    reload(true);
}

void AnnouncementsTabController::updatePageButtons(int)
{
    pageButtonClass *buttons[] = {
        m_ui->pageButton1,
        m_ui->pageButton2,
        m_ui->pageButton3,
        m_ui->pageButton4,
        m_ui->pageButton5
    };
    const int buttonsCount = 5;
    const TablePaginationUiModel model = m_tableController->paginationUiModel();

    for (int i = 0; i < buttonsCount; i++) {
        disconnect(buttons[i], &pageButtonClass::clickedPage, this, &AnnouncementsTabController::getPage);
    }
    disconnect(m_ui->firstPageButton, &pageButtonClass::clickedPage, this, &AnnouncementsTabController::getPage);
    disconnect(m_ui->previousPageButton, &pageButtonClass::clickedPage, this, &AnnouncementsTabController::getPage);
    disconnect(m_ui->nextPageButton, &pageButtonClass::clickedPage, this, &AnnouncementsTabController::getPage);
    disconnect(m_ui->lastPageButton, &pageButtonClass::clickedPage, this, &AnnouncementsTabController::getPage);
    disconnect(m_ui->otherPageButton, &pageButtonClass::clicked, this, &AnnouncementsTabController::getPageFromMiddle);

    m_ui->firstPageButton->setText(QStringLiteral("<<"));
    m_ui->firstPageButton->setPage(1);
    connect(m_ui->firstPageButton, &pageButtonClass::clickedPage, this, &AnnouncementsTabController::getPage);

    m_ui->previousPageButton->setText(QStringLiteral("Prev"));
    if (model.canPrevious) {
        m_ui->previousPageButton->setPage(model.previousPage);
        connect(m_ui->previousPageButton, &pageButtonClass::clickedPage, this, &AnnouncementsTabController::getPage);
    }

    m_ui->nextPageButton->setText(QStringLiteral("Next"));
    if (model.canNext) {
        m_ui->nextPageButton->setPage(model.nextPage);
        connect(m_ui->nextPageButton, &pageButtonClass::clickedPage, this, &AnnouncementsTabController::getPage);
    }

    m_ui->lastPageButton->setText(QString::number(model.lastPage));
    m_ui->lastPageButton->setPage(model.lastPage);
    connect(m_ui->lastPageButton, &pageButtonClass::clickedPage, this, &AnnouncementsTabController::getPage);

    m_ui->otherPageButton->setText(QStringLiteral("..."));

    for (int i = 0; i < buttonsCount; ++i) {
        if (i < model.visiblePages.size()) {
            const int pageNum = model.visiblePages[i];
            buttons[i]->setVisible(true);
            buttons[i]->setText(QString::number(pageNum));
            buttons[i]->setPage(pageNum);
            connect(buttons[i], &pageButtonClass::clickedPage, this, &AnnouncementsTabController::getPage);
            if (model.currentPage == pageNum) {
                buttons[i]->setStyleSheet("QPushButton{"
                                          "font-family: \"Roboto Light\";"
                                          "font-size: 16px;"
                                          "background-color: #0087FC;"
                                          "color: #ffffff;"
                                          "border: 1px solid #0087FC;"
                                          "}");
            } else {
                buttons[i]->setStyleSheet("QPushButton{"
                                           "font-family: \"Roboto Light\";"
                                           "font-size: 16px;"
                                           "background-color: #FFFEFB;"
                                           "color: #c6c6c6;"
                                           "border: 1px solid #B8B290;"
                                           "}"
                                           "QPushButton::hover{"
                                           "background-color: #ffffff;"
                                           "border: 1px solid #0087FC;"
                                           "color: #0087FC;"
                                           "}");
            }
        } else {
            buttons[i]->setVisible(false);
        }
    }

    m_ui->otherPageButton->setVisible(model.showOther);
    if (model.showOther) {
        connect(m_ui->otherPageButton, &pageButtonClass::clicked, this, &AnnouncementsTabController::getPageFromMiddle);
    }
    m_ui->lastPageButton->setVisible(model.showLast);

    setupPageButtonSizes();
}

void AnnouncementsTabController::setupPageButtonSizes()
{
    const int height = m_ui->pageButton1->height();
    foreach (QWidget *widget, m_ui->frame_12->findChildren<QWidget*>()) {
        widget->setMinimumWidth(height + 2);
        widget->setMaximumWidth(height + 2);
        if (m_tableUiState.totalPages > 5) {
            QFont font = widget->font();
            font.setPointSize(font.pointSize() - 2);
            widget->setFont(font);
        }
    }
}
