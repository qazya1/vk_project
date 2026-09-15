#include "adduserdialog.h"
#include "ui_adduserdialog.h"
#include <QDebug>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QErrorMessage>

// Читает client_id из текстового файла.
// Возвращает строку с client_id или пустую строку, если файл не найден.
QString readClientIdFromFile(const QString &filename = "client_id.txt")
{
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();

    QTextStream in(&file);
    QString content = in.readAll().trimmed();
    if (content.isEmpty())
        return QString();

    // Удаляем все пробелы и переводы строк
    content.remove(QRegExp("\\s"));
    return content;
}

addUserDialog::addUserDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::addUserDialog),
    m_vkAuth(new VkOAuth2(this))
{
    ui->setupUi(this);

    ui->clientIDEdit->setPlainText(readClientIdFromFile());

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(ui->generateTokensButton, &QPushButton::clicked, this, &addUserDialog::generateTokens);
    connect(m_vkAuth, &VkOAuth2::success, this, &addUserDialog::onAuthSuccess);
    connect(m_vkAuth, &VkOAuth2::error, this, &addUserDialog::onAuthError);
}

addUserDialog::~addUserDialog()
{
    delete ui;
}

void addUserDialog::getInputText(QString &groupLink, QString &groupName, QString &refreshToken, QString &clientId, QString &deviceId)
{
    groupLink = ui->groupLinkEdit->text();
    refreshToken = ui->refreshTokenEdit->toPlainText();
    groupName = ui->groupNameEdit->text();
    clientId = ui->clientIDEdit->toPlainText();
    deviceId = ui->deviceIDEdit->toPlainText();
}

void addUserDialog::setText(const QString &groupLink, const QString &groupName, const QString &refreshToken, const QString &clientId, const QString &deviceId)
{
    ui->groupLinkEdit->setText(groupLink);
    ui->refreshTokenEdit->setPlainText(refreshToken);
    ui->groupNameEdit->setText(groupName);
    ui->clientIDEdit->setPlainText(clientId);
    ui->deviceIDEdit->setPlainText(deviceId);
}

bool addUserDialog::checkInputText()
{
   if (ui->groupLinkEdit->text().isEmpty() || ui->refreshTokenEdit->toPlainText().isEmpty() || ui->clientIDEdit->toPlainText().isEmpty() || ui->deviceIDEdit->toPlainText().isEmpty())
   {
       return false;
   }
   QRegularExpression linkRegex("(?<=https://vk.com/club)\\d+");
   QRegularExpressionMatch match = linkRegex.match(ui->groupLinkEdit->text());
   if (match.hasMatch()) return true;
   else
   {
       QRegularExpression linkRegex("(?<=https://m.vk.com/club)\\d+");
       match = linkRegex.match(ui->groupLinkEdit->text());
       if (match.hasMatch()) return true;
   }
   //qDebug() << "It isn't vk's link";
   return false;
}

void addUserDialog::onAuthSuccess(const QString &accessToken, const QString &refreshToken, const QString &deviceId)
{
    ui->refreshTokenEdit->setPlainText(refreshToken);
    ui->deviceIDEdit->setPlainText(deviceId);
}

void addUserDialog::onAuthError(const QString &errorMessage)
{
    QMessageBox::critical(this, "Ошибка", errorMessage);
}


void addUserDialog::generateTokens()
{
    m_vkAuth->setClientId(ui->clientIDEdit->toPlainText());          // ваш client_id
    m_vkAuth->setRedirectPort(80);              // или другой порт, если 80 занят
    m_vkAuth->start();                          // запускает процесс
}
