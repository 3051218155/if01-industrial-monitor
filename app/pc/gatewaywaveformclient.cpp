#include "gatewaywaveformclient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QTcpSocket>
#include <QTimer>

GatewayWaveformClient::GatewayWaveformClient(QObject *parent)
    : QObject(parent)
    , socket_(new QTcpSocket(this))
    , timer_(new QTimer(this))
{
    socket_->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout,
            this, &GatewayWaveformClient::requestWaveform);
    connect(socket_, &QTcpSocket::connected, this, [this] {
        const QJsonObject request{
            {QStringLiteral("cmd"), QStringLiteral("waveform")},
            {QStringLiteral("n"), 96},
        };
        socket_->write(
            QJsonDocument(request).toJson(QJsonDocument::Compact)
            + '\n');
    });
    connect(socket_, &QTcpSocket::readyRead,
            this, &GatewayWaveformClient::receive);
    connect(socket_, &QTcpSocket::errorOccurred, this, [this] {
        if (!gotResponse_) {
            emit waveformError(socket_->errorString());
            scheduleRetry();
        }
    });
    connect(socket_, &QTcpSocket::disconnected, this, [this] {
        if (!gotResponse_)
            scheduleRetry();
    });
}

GatewayWaveformClient::~GatewayWaveformClient()
{
    stop();
}

void GatewayWaveformClient::start(int intervalMs)
{
    intervalMs_ = qMax(500, intervalMs);
    requestNext(0);
}

void GatewayWaveformClient::stop()
{
    timer_->stop();
    socket_->abort();
}

void GatewayWaveformClient::requestNext(int delayMs)
{
    if (!timer_->isActive())
        timer_->start(delayMs);
}

void GatewayWaveformClient::requestWaveform()
{
    if (socket_->state() != QAbstractSocket::UnconnectedState)
        socket_->abort();
    buffer_.clear();
    gotResponse_ = false;
    const QString host = qEnvironmentVariable(
        "FPGA_BOARD_HOST", QStringLiteral("192.168.137.100"));
    const int configuredPort = qEnvironmentVariableIntValue("FPGA_BOARD_PORT");
    const quint16 port = static_cast<quint16>(
        configuredPort > 0 ? configuredPort : 9000);
    socket_->connectToHost(host, port);
}

void GatewayWaveformClient::receive()
{
    buffer_ += socket_->readAll();
    if (buffer_.size() > 131072) {
        emit waveformError(QStringLiteral("板端波形响应过大"));
        socket_->abort();
        scheduleRetry();
        return;
    }
    const int end = buffer_.indexOf('\n');
    if (end < 0)
        return;

    gotResponse_ = true;
    QJsonParseError parseError;
    const QJsonDocument doc =
        QJsonDocument::fromJson(buffer_.left(end), &parseError);
    buffer_.clear();
    socket_->abort();
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        emit waveformError(QStringLiteral("板端波形 JSON 无效"));
        scheduleRetry();
        return;
    }

    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("code")).toInt(-1) != 0) {
        emit waveformError(QStringLiteral("板端拒绝波形请求"));
        scheduleRetry();
        return;
    }
    const QJsonObject data =
        root.value(QStringLiteral("data")).toObject();
    const QJsonArray rawSamples =
        data.value(QStringLiteral("samples")).toArray();
    if (rawSamples.isEmpty()) {
        emit waveformError(QStringLiteral("板端波形为空"));
        scheduleRetry();
        return;
    }

    QVector<double> samples;
    samples.reserve(rawSamples.size());
    for (const QJsonValue &value : rawSamples) {
        if (!value.isDouble())
            continue;
        samples.append(value.toDouble() / 16384.0 * 9.80665);
    }
    if (!samples.isEmpty()) {
        emit samplesReady(
            samples,
            data.value(QStringLiteral("source")).toString(),
            data.value(QStringLiteral("period_us")).toInt(2000));
    }
    requestNext(intervalMs_);
}

void GatewayWaveformClient::scheduleRetry()
{
    gotResponse_ = true;
    requestNext(2500);
}
