#include "showapiserver.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>

namespace {

QString jsonResponse(int status, const QJsonObject &body)
{
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    const char *reason = status == 200 ? "OK"
        : status == 404 ? "Not Found"
                        : "Bad Request";
    return QStringLiteral("HTTP/1.1 %1 %2\r\n"
                          "Content-Type: application/json; charset=utf-8\r\n"
                          "Content-Length: %3\r\n"
                          "Connection: close\r\n"
                          "\r\n")
        .arg(status)
        .arg(QString::fromLatin1(reason))
        .arg(payload.size())
        + QString::fromUtf8(payload);
}

QByteArray readBody(const QByteArray &request, int contentLength)
{
    const int headerEnd = request.indexOf("\r\n\r\n");
    if (headerEnd < 0)
        return {};
    return request.mid(headerEnd + 4, contentLength);
}

} // namespace

ShowApiServer::ShowApiServer(QObject *parent)
    : QObject(parent)
    , server_(new QTcpServer(this))
{
    connect(server_, &QTcpServer::newConnection, this, &ShowApiServer::onNewConnection);
}

ShowApiServer::~ShowApiServer() = default;

bool ShowApiServer::listen(quint16 port)
{
    if (!server_->listen(QHostAddress::Any, port))
        return false;
    return true;
}

void ShowApiServer::setRequestHandler(
    std::function<QJsonObject(
        const QString &method,
        const QString &path,
        const QJsonObject &payload)> handler)
{
    handler_ = std::move(handler);
}

void ShowApiServer::onNewConnection()
{
    while (server_->hasPendingConnections()) {
        auto *socket = server_->nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { handleClient(socket); });
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    }
}

void ShowApiServer::handleClient(QTcpSocket *socket)
{
    const QByteArray request = socket->readAll();
    if (request.isEmpty())
        return;

    const QList<QByteArray> lines = request.split('\n');
    if (lines.isEmpty()) {
        socket->write(jsonResponse(400, {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("bad_request")}}).toUtf8());
        socket->disconnectFromHost();
        return;
    }

    const QString requestLine = QString::fromUtf8(lines.at(0)).trimmed();
    const QStringList requestParts = requestLine.split(' ');
    const bool isGet = requestLine.startsWith(QStringLiteral("GET "));
    const bool isPost = requestLine.startsWith(QStringLiteral("POST "));
    const QString method = requestParts.value(0);
    const QString path = requestParts.value(1).section('?', 0, 0);

    int contentLength = 0;
    for (const QByteArray &line : lines) {
        const QString h = QString::fromUtf8(line.trimmed());
        if (h.startsWith(QStringLiteral("Content-Length:"), Qt::CaseInsensitive)) {
            contentLength = h.section(':', 1).trimmed().toInt();
            break;
        }
    }

    if (isGet && requestLine.contains(QStringLiteral("/health"))) {
        socket->write(jsonResponse(200, {{QStringLiteral("ok"), true}}).toUtf8());
        socket->disconnectFromHost();
        return;
    }

    if (!isGet && !isPost) {
        socket->write(jsonResponse(400, {{QStringLiteral("ok"), false},
                                         {QStringLiteral("error"),
                                          QStringLiteral("unsupported_method")}}).toUtf8());
        socket->disconnectFromHost();
        return;
    }

    QJsonObject payload;
    if (isPost) {
        const QByteArray bodyBytes = readBody(request, contentLength);
        const QJsonDocument doc = QJsonDocument::fromJson(bodyBytes);
        if (!doc.isObject()) {
            socket->write(jsonResponse(400, {{QStringLiteral("ok"), false},
                                             {QStringLiteral("error"),
                                              QStringLiteral("invalid_json")}}).toUtf8());
            socket->disconnectFromHost();
            return;
        }
        payload = doc.object();
    }

    if (handler_) {
        const QJsonObject result = handler_(method, path, payload);
        socket->write(jsonResponse(200, result).toUtf8());
        socket->disconnectFromHost();
        return;
    }

    if (isGet && requestLine.contains(QStringLiteral("/api/state"))) {
        socket->write(jsonResponse(200, {{QStringLiteral("ok"), false},
                                         {QStringLiteral("error"),
                                          QStringLiteral("handler_not_configured")}}).toUtf8());
        socket->disconnectFromHost();
        return;
    }

    if (!isPost || !requestLine.contains(QStringLiteral("/api/show"))) {
        socket->write(jsonResponse(400, {{QStringLiteral("ok"), false},
                                         {QStringLiteral("error"),
                                          QStringLiteral("use /api/state or /api/command")}}).toUtf8());
        socket->disconnectFromHost();
        return;
    }

    QJsonObject resp{{QStringLiteral("ok"), false},
                     {QStringLiteral("error"),
                      QStringLiteral("diagnosis_override_disabled")}};
    socket->write(jsonResponse(200, resp).toUtf8());
    socket->disconnectFromHost();
}
