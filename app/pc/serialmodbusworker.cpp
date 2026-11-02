#include "serialmodbusworker.h"

#include "serialmodbusclient.h"

SerialModbusWorker::SerialModbusWorker(QObject *parent)
    : QObject(parent)
    , serial_(new SerialModbusClient(this))
{
}

SerialModbusWorker::~SerialModbusWorker()
{
    serial_->close();
}

void SerialModbusWorker::openPort(const QString &port, int baud, int slave)
{
    port_ = port.trimmed();
    baud_ = baud;
    slave_ = slave;
    if (port_.isEmpty()) {
        emit serialError(slave_, QStringLiteral("没有可用的串口"));
        return;
    }

    QString error;
    if (!ensureOpen(&error))
        emit serialError(slave_, error);
}

void SerialModbusWorker::closePort()
{
    serial_->close();
    port_.clear();
}

void SerialModbusWorker::poll(int slave)
{
    publishRead(slave);
}

void SerialModbusWorker::startMotor(int slave, quint16 pwm)
{
    writeControl(true, pwm, slave);
}

void SerialModbusWorker::stopMotor(int slave)
{
    writeControl(false, 0, slave);
}

void SerialModbusWorker::writeRegisters(
    int slave,
    const QVector<quint16> &registers,
    const QVector<quint16> &values)
{
    if (registers.size() != values.size() || registers.isEmpty()) {
        emit controlFinished(false, QStringLiteral("寄存器写入参数不一致"));
        return;
    }
    QString error;
    if (!ensureOpen(&error)) {
        emit serialError(slave, error);
        emit controlFinished(false, QStringLiteral("寄存器写入失败：%1").arg(error));
        return;
    }
    for (int i = 0; i < registers.size(); ++i) {
        if (!serial_->writeRegister(
                slave, registers[i], values[i], &error)) {
            serial_->close();
            emit serialError(slave, error);
            emit controlFinished(
                false, QStringLiteral("寄存器写入失败：%1").arg(error));
            return;
        }
    }
    emit controlFinished(true, QStringLiteral("工况测点寄存器已写入"));
    publishRead(slave);
}

void SerialModbusWorker::shutdown()
{
    serial_->close();
}

bool SerialModbusWorker::ensureOpen(QString *error)
{
    if (serial_->isOpen() && serial_->portName() == port_)
        return true;

    serial_->close();
    if (port_.isEmpty()) {
        if (error)
            *error = QStringLiteral("没有可用的串口");
        return false;
    }
    if (!serial_->open(port_, baud_, error))
        return false;

    emit portOpened(port_);
    return true;
}

bool SerialModbusWorker::readOnce(
    int slave,
    QVector<quint16> *values,
    QString *error)
{
    if (!ensureOpen(error))
        return false;
    if (!serial_->readRegisters(slave, 0, 32, values, error)) {
        QString twoDeviceBlockError;
        if (serial_->readRegisters(slave, 0, 22, values, &twoDeviceBlockError)) {
            values->resize(32);
            for (int i = 22; i < values->size(); ++i)
                (*values)[i] = 0;
        } else {
            QString legacyError;
            if (!serial_->readRegisters(slave, 0, 16, values, &legacyError)) {
                serial_->close();
                if (error)
                    *error = legacyError.isEmpty()
                        ? QStringLiteral("读取 Modbus 寄存器失败")
                        : legacyError;
                return false;
            }
            values->resize(32);
            for (int i = 16; i < values->size(); ++i)
                (*values)[i] = 0;
        }
    }
    if (!values || values->size() != 32) {
        if (error)
            *error = QStringLiteral("Modbus 返回寄存器数量异常");
        serial_->close();
        return false;
    }
    return true;
}

void SerialModbusWorker::publishRead(int slave)
{
    QVector<quint16> values;
    QString error;
    if (!readOnce(slave, &values, &error)) {
        emit serialError(slave, error);
        return;
    }
    emit registersRead(slave, values, port_);
}

void SerialModbusWorker::writeControl(bool start, quint16 pwm, int slave)
{
    QString error;
    if (!ensureOpen(&error)) {
        emit serialError(slave, error);
        emit controlFinished(false, QStringLiteral("A 路控制失败：%1").arg(error));
        return;
    }

    const bool wrote = start
        ? serial_->writeRegister(slave, 1, pwm, &error)
              && serial_->writeRegister(slave, 0, 1, &error)
        : serial_->writeRegister(slave, 0, 0, &error)
              && serial_->writeRegister(slave, 1, 0, &error);
    if (!wrote) {
        serial_->close();
        emit serialError(slave, error);
        emit controlFinished(false, QStringLiteral("A 路控制失败：%1").arg(error));
        return;
    }

    emit controlFinished(
        true,
        start ? QStringLiteral("A 路已启动 · PWM %1").arg(pwm)
              : QStringLiteral("A 路已停止"));
    publishRead(slave);
}
