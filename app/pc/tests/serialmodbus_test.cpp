#include "serialmodbusclient.h"

#include <QCoreApplication>
#include <QTextStream>
#include <QVector>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    const QString port = qEnvironmentVariable(
        "FPGA_SERIAL_PORT",
        argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("COM10"));

    SerialModbusClient client;
    QString error;
    if (!client.open(port, 9600, &error)) {
        out << "FAIL open: " << error << '\n';
        return 1;
    }

    QVector<quint16> regs;
    if (!client.readRegisters(1, 0, 16, &regs, &error)) {
        out << "FAIL FC03: " << error << '\n';
        return 1;
    }
    if (regs.size() != 16) {
        out << "FAIL FC03 count " << regs.size() << '\n';
        return 1;
    }

    if (!client.writeRegister(1, 1, 0, &error)
        || !client.writeRegister(1, 0, 0, &error)) {
        out << "FAIL FC06: " << error << '\n';
        return 1;
    }

    out << "PASS " << port << " FC03:";
    for (quint16 value : regs)
        out << ' ' << value;
    out << " | FC06 safe zero write PASS\n";
    return 0;
}
