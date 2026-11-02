#include "serialmodbusclient.h"

#include <QElapsedTimer>
#include <QThread>

#include <algorithm>
#include <cstring>
#include <iterator>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

QString windowsError(const QString &action)
{
#ifdef _WIN32
    return QStringLiteral("%1 (Win32 error %2)").arg(action).arg(GetLastError());
#else
    return action;
#endif
}

#ifdef _WIN32
QStringList enumerateSerialPorts()
{
    QStringList ports;
    HKEY key = nullptr;
    const LSTATUS openResult = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DEVICEMAP\\SERIALCOMM",
        0,
        KEY_READ,
        &key);
    if (openResult != ERROR_SUCCESS)
        return ports;

    for (DWORD index = 0;; ++index) {
        wchar_t valueName[256] = {};
        DWORD valueNameLength = static_cast<DWORD>(std::size(valueName));
        wchar_t valueData[256] = {};
        DWORD valueDataSize = static_cast<DWORD>(sizeof(valueData));
        DWORD valueType = 0;
        const LSTATUS result = RegEnumValueW(
            key,
            index,
            valueName,
            &valueNameLength,
            nullptr,
            &valueType,
            reinterpret_cast<BYTE *>(valueData),
            &valueDataSize);
        if (result == ERROR_NO_MORE_ITEMS)
            break;
        if (result != ERROR_SUCCESS)
            continue;
        if (valueType != REG_SZ && valueType != REG_EXPAND_SZ)
            continue;

        valueData[std::size(valueData) - 1] = L'\0';
        const QString port =
            QString::fromWCharArray(valueData).trimmed().toUpper();
        if (!port.isEmpty() && !ports.contains(port))
            ports.append(port);
    }

    RegCloseKey(key);
    return ports;
}
#endif

} // namespace

SerialModbusClient::SerialModbusClient(QObject *parent)
    : QObject(parent)
{
}

SerialModbusClient::~SerialModbusClient()
{
    close();
}

QStringList SerialModbusClient::availablePorts()
{
    QStringList ports;
#ifdef _WIN32
    ports = enumerateSerialPorts();
#endif
    const QString preferred = qEnvironmentVariable(
        "FPGA_SERIAL_PORT", QStringLiteral("COM10"));
    if (!preferred.isEmpty() && !ports.contains(preferred))
        ports.append(preferred);
    std::sort(ports.begin(), ports.end(), [](const QString &a, const QString &b) {
        const int na = a.mid(3).toInt();
        const int nb = b.mid(3).toInt();
        return na == nb ? a < b : na < nb;
    });
    return ports;
}

bool SerialModbusClient::open(const QString &port, int baud, QString *error)
{
    close();
#ifdef _WIN32
    const QString devicePath = QStringLiteral("\\\\.\\") + port;
    HANDLE handle = CreateFileW(
        reinterpret_cast<LPCWSTR>(devicePath.utf16()),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        setError(error, windowsError(QStringLiteral("无法打开串口 %1").arg(port)));
        return false;
    }

    SetupComm(handle, 4096, 4096);
    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(handle, &dcb)) {
        const QString message = windowsError(QStringLiteral("读取串口参数失败"));
        CloseHandle(handle);
        setError(error, message);
        return false;
    }
    dcb.BaudRate = static_cast<DWORD>(baud);
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    if (!SetCommState(handle, &dcb)) {
        const QString message = windowsError(QStringLiteral("设置串口参数失败"));
        CloseHandle(handle);
        setError(error, message);
        return false;
    }

    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = 20;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 500;
    if (!SetCommTimeouts(handle, &timeouts)) {
        const QString message = windowsError(QStringLiteral("设置串口超时失败"));
        CloseHandle(handle);
        setError(error, message);
        return false;
    }
    PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    handle_ = reinterpret_cast<void *>(handle);
    port_ = port;
    return true;
#else
    Q_UNUSED(port);
    Q_UNUSED(baud);
    setError(error, QStringLiteral("当前平台未实现原生串口客户端"));
    return false;
#endif
}

void SerialModbusClient::close()
{
#ifdef _WIN32
    if (handle_)
        CloseHandle(reinterpret_cast<HANDLE>(handle_));
#endif
    handle_ = nullptr;
    port_.clear();
}

bool SerialModbusClient::isOpen() const
{
    return handle_ != nullptr;
}

QString SerialModbusClient::portName() const
{
    return port_;
}

quint16 SerialModbusClient::crc16(const QByteArray &data)
{
    quint16 crc = 0xFFFF;
    for (unsigned char value : data) {
        crc ^= value;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 1) ? static_cast<quint16>((crc >> 1) ^ 0xA001)
                            : static_cast<quint16>(crc >> 1);
    }
    return crc;
}

void SerialModbusClient::appendCrc(QByteArray *data)
{
    const quint16 crc = crc16(*data);
    data->append(static_cast<char>(crc & 0xFF));
    data->append(static_cast<char>((crc >> 8) & 0xFF));
}

bool SerialModbusClient::validateFrame(
    const QByteArray &frame,
    int slave,
    int function,
    QString *error)
{
    if (frame.size() < 5) {
        setError(error, QStringLiteral("Modbus 响应过短: %1")
                            .arg(QString::fromLatin1(frame.toHex(' '))));
        return false;
    }
    const quint16 received =
        static_cast<quint8>(frame.at(frame.size() - 2))
        | (static_cast<quint16>(
               static_cast<quint8>(frame.at(frame.size() - 1))) << 8);
    if (received != crc16(frame.left(frame.size() - 2))) {
        setError(error, QStringLiteral("Modbus CRC 错误"));
        return false;
    }
    if (static_cast<quint8>(frame[0]) != slave) {
        setError(error, QStringLiteral("Modbus 从站地址不匹配"));
        return false;
    }
    if (static_cast<quint8>(frame[1]) & 0x80) {
        setError(error, QStringLiteral("Modbus 异常码 0x%1")
                            .arg(static_cast<quint8>(frame[2]), 2, 16, QLatin1Char('0')));
        return false;
    }
    if (static_cast<quint8>(frame[1]) != function) {
        setError(error, QStringLiteral("Modbus 功能码不匹配"));
        return false;
    }
    return true;
}

bool SerialModbusClient::transact(
    const QByteArray &request,
    int expectedLength,
    QByteArray *response,
    QString *error)
{
#ifdef _WIN32
    static thread_local QElapsedTimer lastTransactionEnd;
    constexpr qint64 kRtuGapMs = 15;
    if (lastTransactionEnd.isValid()) {
        const qint64 remaining = kRtuGapMs - lastTransactionEnd.elapsed();
        if (remaining > 0)
            QThread::msleep(static_cast<unsigned long>(remaining));
    }
    struct EndGuard {
        QElapsedTimer *timer;
        ~EndGuard() { timer->restart(); }
    } endGuard{&lastTransactionEnd};

    if (!handle_) {
        setError(error, QStringLiteral("串口未打开"));
        return false;
    }
    HANDLE handle = reinterpret_cast<HANDLE>(handle_);
    PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    DWORD written = 0;
    if (!WriteFile(handle, request.constData(),
                   static_cast<DWORD>(request.size()), &written, nullptr)
        || written != static_cast<DWORD>(request.size())) {
        setError(error, windowsError(QStringLiteral("串口发送失败")));
        return false;
    }

    response->clear();
    QElapsedTimer timer;
    timer.start();
    while (response->size() < expectedLength && timer.elapsed() < 350) {
        char buffer[256];
        const DWORD wanted = static_cast<DWORD>(
            std::min<int>(sizeof(buffer), expectedLength - response->size()));
        DWORD read = 0;
        if (!ReadFile(handle, buffer, wanted, &read, nullptr)) {
            setError(error, windowsError(QStringLiteral("串口接收失败")));
            return false;
        }
        if (read > 0)
            response->append(buffer, static_cast<int>(read));
    }
    if (response->size() != expectedLength) {
        setError(error, QStringLiteral("Modbus 响应长度 %1，期望 %2")
                            .arg(response->size()).arg(expectedLength));
        return false;
    }
    return true;
#else
    Q_UNUSED(request);
    Q_UNUSED(expectedLength);
    Q_UNUSED(response);
    setError(error, QStringLiteral("当前平台未实现原生串口客户端"));
    return false;
#endif
}

bool SerialModbusClient::readRegisters(
    int slave,
    int start,
    int count,
    QVector<quint16> *values,
    QString *error)
{
    if (!values || count < 1 || count > 125) {
        setError(error, QStringLiteral("非法寄存器数量"));
        return false;
    }
    QByteArray request;
    request.append(static_cast<char>(slave & 0xFF));
    request.append(static_cast<char>(0x03));
    request.append(static_cast<char>((start >> 8) & 0xFF));
    request.append(static_cast<char>(start & 0xFF));
    request.append(static_cast<char>((count >> 8) & 0xFF));
    request.append(static_cast<char>(count & 0xFF));
    appendCrc(&request);

    QByteArray response;
    if (!transact(request, 5 + count * 2, &response, error))
        return false;
    if (!validateFrame(response, slave, 0x03, error))
        return false;
    if (static_cast<quint8>(response[2]) != count * 2) {
        setError(error, QStringLiteral("Modbus 字节数不匹配"));
        return false;
    }
    values->clear();
    values->reserve(count);
    for (int i = 0; i < count; ++i) {
        const int offset = 3 + i * 2;
        values->append(
            (static_cast<quint16>(static_cast<quint8>(response[offset])) << 8)
            | static_cast<quint8>(response[offset + 1]));
    }
    return true;
}

bool SerialModbusClient::writeRegister(
    int slave,
    int reg,
    quint16 value,
    QString *error)
{
    QByteArray request;
    request.append(static_cast<char>(slave & 0xFF));
    request.append(static_cast<char>(0x06));
    request.append(static_cast<char>((reg >> 8) & 0xFF));
    request.append(static_cast<char>(reg & 0xFF));
    request.append(static_cast<char>((value >> 8) & 0xFF));
    request.append(static_cast<char>(value & 0xFF));
    appendCrc(&request);

    QByteArray response;
    if (!transact(request, request.size(), &response, error))
        return false;
    if (!validateFrame(response, slave, 0x06, error))
        return false;
    const quint16 echoed = (
        static_cast<quint16>(static_cast<quint8>(response[4])) << 8)
        | static_cast<quint8>(response[5]);
    if (echoed != value) {
        setError(error, QStringLiteral("Modbus 写回读不一致"));
        return false;
    }
    return true;
}
