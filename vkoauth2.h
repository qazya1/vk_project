#ifndef VKOAUTH2_H
#define VKOAUTH2_H

#include <QObject>
#include <QTcpServer>
#include <QNetworkAccessManager>
#include <QUrl>
#include <QString>
#include <QTimer>

class VkOAuth2 : public QObject
{
    Q_OBJECT
public:
    explicit VkOAuth2(QObject *parent = nullptr);
    ~VkOAuth2();

    // Установить параметры (обязательно перед вызовом start())
    void setClientId(const QString &clientId);
    void setRedirectPort(quint16 port = 80);   // по умолчанию 80, можно изменить

    // Начать процесс авторизации
    void start();

signals:
    // Успешное завершение – возвращает access_token, refresh_token, device_id
    void success(const QString &accessToken, const QString &refreshToken, const QString &deviceId);

    // Ошибка – с текстовым описанием
    void error(const QString &errorMessage);

    // Опционально: код авторизации (если нужно)
    void authCodeReceived(const QString &code);

private slots:
    void onNewConnection();
    void onNetworkReplyFinished(QNetworkReply *reply);
    void onServerTimeout();

private:
    // PKCE generation
    QString generateCodeVerifier() const;
    QString generateCodeChallenge(const QString &verifier) const;
    QString generateState() const;
    QString base64UrlEncode(const QByteArray &data) const;
    QString randomString(int minLen, int maxLen) const;

    // HTTP helpers
    void startLocalServer();
    void stopLocalServer();
    void openBrowser();
    void exchangeCodeForTokens(const QString &code, const QString &deviceId);

    // Данные
    QString m_clientId;
    QString m_redirectUri;
    quint16 m_port;

    QString m_codeVerifier;
    QString m_state;
    QString m_deviceId;     // сохраняем из callback

    // Сеть
    QTcpServer *m_server;
    QNetworkAccessManager *m_nam;
    QTimer *m_timeoutTimer;
};

#endif // VKOAUTH2_H
