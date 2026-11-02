#pragma once

#include <QByteArray>
#include <QObject>
#include <QVector>

class QTcpSocket;
class QTimer;

class GatewayWaveformClient final : public QObject
{
    Q_OBJECT

public:
    explicit GatewayWaveformClient(QObject *parent = nullptr);
    ~GatewayWaveformClient() override;

    void start(int intervalMs = 1500);
    void stop();

signals:
    void samplesReady(
        const QVector<double> &samples,
        const QString &source,
        int periodUs);
    void waveformError(const QString &message);

private:
    void requestNext(int delayMs);
    void requestWaveform();
    void receive();
    void scheduleRetry();

    QTcpSocket *socket_;
    QTimer *timer_;
    QByteArray buffer_;
    int intervalMs_ = 1500;
    bool gotResponse_ = false;
};
