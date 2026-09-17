#include "xmlimporttabcontroller.h"

#include "ui_mainwindow.h"
#include "announcementservice.h"

#include <QErrorMessage>
#include <QFileDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QWidget>

XmlImportTabController::XmlImportTabController(Ui::MainWindow *ui,
                                               AnnouncementService *announcementService,
                                               QWidget *dialogParent,
                                               QObject *parent)
    : QObject(parent)
    , m_ui(ui)
    , m_announcementService(announcementService)
    , m_dialogParent(dialogParent)
{
}

void XmlImportTabController::setup()
{
    connect(m_ui->dialogXmlFileButon, &QPushButton::clicked, this, &XmlImportTabController::chooseXmlFile);
    connect(m_ui->importXmlButton, &QPushButton::clicked, this, &XmlImportTabController::importXml);
}

void XmlImportTabController::chooseXmlFile()
{
    const QString fileName = QFileDialog::getOpenFileName(
        m_dialogParent,
        QStringLiteral("Выберите файл"),
        QString(),
        QStringLiteral("XML files (*.xml);;All files (*.*)"));

    if (!fileName.isEmpty()) {
        m_ui->pathXmlEdit->setText(QString::fromUtf8(fileName.toUtf8()));
    }
}

void XmlImportTabController::importXml()
{
    const QString xmlAddress = m_ui->pathXmlEdit->text();
    if (xmlAddress.isEmpty()) {
        QErrorMessage *errorMessage = new QErrorMessage(m_dialogParent);
        errorMessage->showMessage(QStringLiteral("Выберите файл!"));
        return;
    }

    if (!m_announcementService->importXml(xmlAddress)) {
        QErrorMessage *errorMessage = new QErrorMessage(m_dialogParent);
        errorMessage->showMessage(QStringLiteral("Не удалось импортировать данные!"));
        return;
    }

    QMessageBox::information(m_dialogParent, QStringLiteral("Успех"), QStringLiteral("Данные успешно импортированы!"));
    emit xmlImportedSuccessfully();
}
