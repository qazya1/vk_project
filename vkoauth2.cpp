#include "vkoauth2.h"
#include <QTcpSocket>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QDesktopServices>
#include <QRandomGenerator>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

VkOAuth2::VkOAuth2(QObject *parent)
    : QObject(parent)
    , m_port(80)
    , m_server(nullptr)
    , m_nam(new QNetworkAccessManager(this))
    , m_timeoutTimer(new QTimer(this))
{
    m_timeoutTimer->setSingleShot(true);
    connect(m_timeoutTimer, &QTimer::timeout, this, &VkOAuth2::onServerTimeout);
}

VkOAuth2::~VkOAuth2()
{
    stopLocalServer();
}

void VkOAuth2::setClientId(const QString &clientId)
{
    m_clientId = clientId;
}

void VkOAuth2::setRedirectPort(quint16 port)
{
    m_port = port;
    m_redirectUri = QString("http://localhost");
}

void VkOAuth2::start()
{
    if (m_clientId.isEmpty()) {
        emit error("Client ID не установлен");
        return;
    }
    if (m_port == 0) {
        setRedirectPort(80); // значение по умолчанию
    }

    // Генерируем PKCE параметры
    m_codeVerifier = generateCodeVerifier();
    m_state = generateState();

    // Запускаем локальный сервер
    startLocalServer();
    if (!m_server || !m_server->isListening()) {
        emit error("Не удалось запустить локальный сервер на порту " + QString::number(m_port));
        return;
    }

    // Открываем браузер
    openBrowser();
}

void VkOAuth2::startLocalServer()
{
    stopLocalServer();
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &VkOAuth2::onNewConnection);
    if (!m_server->listen(QHostAddress::LocalHost, m_port)) {
        emit error("Ошибка привязки к порту: " + m_server->errorString());
        delete m_server;
        m_server = nullptr;
        return;
    }
    m_timeoutTimer->start(120000); // 2 минуты на авторизацию
}

void VkOAuth2::stopLocalServer()
{
    if (m_server) {
        m_server->close();
        delete m_server;
        m_server = nullptr;
    }
    m_timeoutTimer->stop();
}

void VkOAuth2::openBrowser()
{
    QString codeChallenge = generateCodeChallenge(m_codeVerifier);
    QUrl url("https://id.vk.com/authorize");
    QUrlQuery query;
    query.addQueryItem("response_type", "code");
    query.addQueryItem("client_id", m_clientId);
    query.addQueryItem("redirect_uri", m_redirectUri);
    query.addQueryItem("state", m_state);
    query.addQueryItem("code_challenge", codeChallenge);
    query.addQueryItem("code_challenge_method", "S256");
    query.addQueryItem("scope", "wall groups"); // можно сделать параметром
    url.setQuery(query);
    QDesktopServices::openUrl(url);
}

void VkOAuth2::onNewConnection()
{
    QTcpSocket *socket = m_server->nextPendingConnection();
    if (!socket) return;

    // Читаем запрос асинхронно
    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
        QByteArray data = socket->readAll();
        QString request(data);

        // Ищем GET /?code=...&state=...&device_id=...
        if (!request.startsWith("GET /")) {
            socket->close();
            return;
        }

        int queryStart = request.indexOf('?');
        int queryEnd = request.indexOf(' ', queryStart);
        if (queryStart == -1 || queryEnd == -1) {
            socket->close();
            return;
        }

        QString queryString = request.mid(queryStart + 1, queryEnd - queryStart - 1);
        QUrlQuery urlQuery(queryString);
        QString code = urlQuery.queryItemValue("code");
        QString state = urlQuery.queryItemValue("state");
        QString deviceId = urlQuery.queryItemValue("device_id");

        // Проверяем state
        if (state != m_state) {
            socket->write("HTTP/1.1 400 Bad Request\r\n\r\nState mismatch");
            socket->flush();
            socket->close();
            emit error("Несовпадение state – возможно, подмена запроса");
            stopLocalServer();
            return;
        }

        // Отправляем успешный ответ
        QString replyText = "Локальный сервер принял и обработал данные";
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\n\r\n");
        socket->write(replyText.toUtf8());
        socket->flush();
        socket->close();

        // Останавливаем таймаут
        m_timeoutTimer->stop();
        m_deviceId = deviceId;

        emit authCodeReceived(code);

        // Обмениваем код на токены
        exchangeCodeForTokens(code, deviceId);
    });

    connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
}

void VkOAuth2::exchangeCodeForTokens(const QString &code, const QString &deviceId)
{
    QUrl url("https://id.vk.com/oauth2/auth");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QUrlQuery params;
    params.addQueryItem("client_id", m_clientId);
    params.addQueryItem("grant_type", "authorization_code");
    params.addQueryItem("code_verifier", m_codeVerifier);
    params.addQueryItem("device_id", deviceId);
    params.addQueryItem("code", code);
    params.addQueryItem("redirect_uri", m_redirectUri);
    params.addQueryItem("state", m_state);

    QNetworkReply *reply = m_nam->post(request, params.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onNetworkReplyFinished(reply);
    });
}

void VkOAuth2::onNetworkReplyFinished(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        emit error("Сетевая ошибка: " + reply->errorString());
        stopLocalServer();
        return;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        emit error("Неверный JSON от сервера");
        stopLocalServer();
        return;
    }

    QJsonObject obj = doc.object();
    QString returnedState = obj.value("state").toString();
    if (returnedState != m_state) {
        emit error("State не совпадает – возможна подмена");
        stopLocalServer();
        return;
    }

    QString accessToken = obj.value("access_token").toString();
    QString refreshToken = obj.value("refresh_token").toString();
    if (accessToken.isEmpty() || refreshToken.isEmpty()) {
        emit error("Ответ не содержит токенов: " + QString::fromUtf8(data));
        stopLocalServer();
        return;
    }

    emit success(accessToken, refreshToken, m_deviceId);
    stopLocalServer(); // закрываем сервер после успеха
}

void VkOAuth2::onServerTimeout()
{
    stopLocalServer();
    emit error("Превышено время ожидания авторизации (2 минуты)");
}

// ---------- PKCE helpers ----------
QString VkOAuth2::randomString(int minLen, int maxLen) const
{
    int len = QRandomGenerator::global()->bounded(minLen, maxLen + 1);
    const QString chars = "abcdefghijkmnlopqrstuvwzyx_-0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    QString result;
    result.reserve(len);
    for (int i = 0; i < len; ++i) {
        int idx = QRandomGenerator::global()->bounded(chars.size());
        result.append(chars.at(idx));
    }
    return result;
}

QString VkOAuth2::base64UrlEncode(const QByteArray &data) const
{
    return data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QString VkOAuth2::generateCodeVerifier() const
{
    return randomString(43, 128);
}

QString VkOAuth2::generateCodeChallenge(const QString &verifier) const
{
    QByteArray hash = QCryptographicHash::hash(verifier.toUtf8(), QCryptographicHash::Sha256);
    return base64UrlEncode(hash);
}

QString VkOAuth2::generateState() const
{
    return randomString(32, 64);
}
