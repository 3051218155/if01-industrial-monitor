#include "boardalertclient.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QTcpSocket>
#include <QTimer>

BoardAlertClient::BoardAlertClient(QObject *parent)
    : QObject(parent)
    , socket_(new QTcpSocket(this))
    , retryTimer_(new QTimer(this))
    , desiredLevel_(0)
    , acknowledgedLevel_(-2)
    , hasDesiredLevel_(false)
    , requestInFlight_(false)
{
    socket_->setProxy(
        QNetworkProxy(QNetworkProxy::NoProxy));
    retryTimer_->setSingleShot(true);
    retryTimer_->setInterval(2500);
    connect(retryTimer_, &QTimer::timeout,
            this, &BoardAlertClient::ensureSend);
    connect(socket_, &QTcpSocket::connected,
            this, &BoardAlertClient::ensureSend);
    connect(socket_, &QTcpSocket::readyRead,
            this, &BoardAlertClient::consumeResponses);
    connect(socket_, &QTcpSocket::disconnected, this, [this] {
        requestInFlight_ = false;
        scheduleRetry();
    });
    connect(socket_, &QTcpSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) {
        qWarning().noquote()
            << "BoardAlertClient error"
            << socket_->errorString();
        requestInFlight_ = false;
        scheduleRetry();
    });
}

void BoardAlertClient::setLevel(int level)
{
    if (level < -1 || level > 2)
        return;
    if (!hasDesiredLevel_ || desiredLevel_ != level) {
        qInfo().noquote()
            << "BoardAlertClient level" << level;
    }
    desiredLevel_ = level;
    hasDesiredLevel_ = true;
    ensureSend();
}

void BoardAlertClient::ensureSend()
{
    if (!hasDesiredLevel_
        || requestInFlight_
        || acknowledgedLevel_ == desiredLevel_) {
        return;
    }

    if (socket_->state() == QAbstractSocket::ConnectedState) {
        sendRequest();
        return;
    }
    if (socket_->state() == QAbstractSocket::ConnectingState
        || socket_->state() == QAbstractSocket::HostLookupState) {
        return;
    }

    const QString host = qEnvironmentVariable(
        "FPGA_BOARD_HOST", QStringLiteral("192.168.137.100"));
    const quint16 port = static_cast<quint16>(
        qEnvironmentVariableIntValue("FPGA_BOARD_PORT") > 0
            ? qEnvironmentVariableIntValue("FPGA_BOARD_PORT")
            : 9000);
    qInfo().noquote()
        << "BoardAlertClient connecting" << host << port;
    socket_->connectToHost(host, port);
}

void BoardAlertClient::sendRequest()
{
    const QByteArray request = QByteArrayLiteral("{\"cmd\":\"force_alert\",\"level\":")
        + QByteArray::number(desiredLevel_) + QByteArrayLiteral("}\n");
    requestInFlight_ = true;
    qInfo().noquote()
        << "BoardAlertClient send" << QString::fromUtf8(request).trimmed();
    socket_->write(request);
}

void BoardAlertClient::scheduleRetry()
{
    if (hasDesiredLevel_ && acknowledgedLevel_ != desiredLevel_)
        retryTimer_->start();
}

void BoardAlertClient::consumeResponses()
{
    while (socket_->canReadLine()) {
        const QByteArray line = socket_->readLine().trimmed();
        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isObject())
            continue;
        const QJsonObject data = doc.object()
                                     .value(QStringLiteral("data"))
                                     .toObject();
        if (!data.contains(QStringLiteral("force_level")))
            continue;

        acknowledgedLevel_ =
            data.value(QStringLiteral("force_level")).toInt();
        qInfo().noquote()
            << "BoardAlertClient acknowledged"
            << acknowledgedLevel_;
        requestInFlight_ = false;
        ensureSend();
    }
}
