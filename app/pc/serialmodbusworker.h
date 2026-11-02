#pragma once

#include <QObject>
#include <QString>
#include <QVector>

class SerialModbusClient;

class SerialModbusWorker final : public QObject
{
    Q_OBJECT

public:
    explicit SerialModbusWorker(QObject *parent = nullptr);
    ~SerialModbusWorker() override;

public slots:
    void openPort(const QString &port, int baud, int slave);
    void closePort();
    void poll(int slave);
    void startMotor(int slave, quint16 pwm);
    void stopMotor(int slave);
    void writeRegisters(
        int slave,
        const QVector<quint16> &registers,
        const QVector<quint16> &values);
    void shutdown();

signals:
    void portOpened(const QString &port);
    void registersRead(
        int slave,
        const QVector<quint16> &values,
        const QString &port);
    void serialError(int slave, const QString &message);
    void controlFinished(bool ok, const QString &message);

private:
    bool ensureOpen(QString *error);
    bool readOnce(int slave, QVector<quint16> *values, QString *error);
    void publishRead(int slave);
    void writeControl(bool start, quint16 pwm, int slave);

    SerialModbusClient *serial_;
    QString port_;
    int baud_ = 9600;
    int slave_ = 1;
};
