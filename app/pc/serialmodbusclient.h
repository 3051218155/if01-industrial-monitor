#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

class SerialModbusClient : public QObject
{
public:
    explicit SerialModbusClient(QObject *parent = nullptr);
    ~SerialModbusClient() override;

    static QStringList availablePorts();

    bool open(const QString &port, int baud, QString *error = nullptr);
    void close();
    bool isOpen() const;
    QString portName() const;

    bool readRegisters(
        int slave,
        int start,
        int count,
        QVector<quint16> *values,
        QString *error = nullptr);
    bool writeRegister(
        int slave,
        int reg,
        quint16 value,
        QString *error = nullptr);

private:
    bool transact(
        const QByteArray &request,
        int expectedLength,
        QByteArray *response,
        QString *error);
    static quint16 crc16(const QByteArray &data);
    static void appendCrc(QByteArray *data);
    static bool validateFrame(
        const QByteArray &frame,
        int slave,
        int function,
        QString *error);

    void *handle_ = nullptr;
    QString port_;
};
