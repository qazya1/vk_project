#ifndef ADDUSERDIALOG_H
#define ADDUSERDIALOG_H

#include <QDialog>
#include <QString>
#include <vkoauth2.h>
#include <QFile>
#include <QTextStream>
#include <QString>

namespace Ui {
class addUserDialog;
}

class addUserDialog : public QDialog
{
    Q_OBJECT

public:
    explicit addUserDialog(QWidget *parent);
    ~addUserDialog();
    void getInputText(QString &groupLink, QString &groupName, QString &refreshToken, QString &clientId, QString &deviceId);
    void setText(const QString &groupLink, const QString &groupName, const QString &refreshToken, const QString &clientId, const QString &deviceId);
    bool checkInputText();

private:
    Ui::addUserDialog *ui;
    void onAuthSuccess(const QString &accessToken, const QString &refreshToken, const QString &deviceId);
    void onAuthError(const QString &errorMessage);
    void generateTokens();
    VkOAuth2 *m_vkAuth;
};

#endif // ADDUSERDIALOG_H
