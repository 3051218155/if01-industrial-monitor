#include "capturewindow.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <functional>
#include <memory>
#include <stdexcept>

static void check(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}

static void waitUntil(const std::function<bool()> &predicate, int timeout = 10000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeout) {
        QApplication::processEvents();
        QThread::msleep(5);
    }
    check(predicate(), "wait timed out");
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    try {
        QTemporaryDir directory;
        check(directory.isValid(), "temporary directory unavailable");
        QTcpServer server;
        check(server.listen(QHostAddress::LocalHost, 9000), "test port 9000 unavailable");
        QString mode = "source_reject";
        QObject::connect(&server, &QTcpServer::newConnection, &app, [&] {
            auto *socket = server.nextPendingConnection();
            auto buffer = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, buffer] {
                *buffer += socket->readAll();
                if (!buffer->contains('\n')) return;
                const auto req = QJsonDocument::fromJson(*buffer).object();
                if (mode == "disconnect") { socket->disconnectFromHost(); return; }
                if (mode == "timeout") return;
                QJsonObject data{{"device_id", 1}};
                if (req.value("cmd") == "query_device") {
                    data.insert("source", mode == "source_reject"
                        ? QStringLiteral("板端数据") : QStringLiteral("MPU6050+PL硬件"));
                } else {
                    QJsonArray samples;
                    for (int i = 0; i < 256; ++i) samples.append(i - 128);
                    if (mode == "malformed") samples[7] = "invalid";
                    data.insert("samples", samples);
                    data.insert("period_us", 2000);
                }
                const auto response = QJsonDocument(QJsonObject{{"code", 0}, {"data", data}})
                    .toJson(QJsonDocument::Compact) + '\n';
                // Exercise fragmented TCP responses.
                QTimer::singleShot(req.value("cmd") == "waveform" ? 512 : 0,
                    socket, [socket, response] {
                        socket->write(response.left(23));
                        socket->flush();
                        QTimer::singleShot(10, socket, [socket, response] {
                            socket->write(response.mid(23));
                        });
                    });
            });
        });
        CaptureWindow window;
        window.findChild<QLineEdit *>("captureHost")->setText("127.0.0.1");
        window.findChild<QLineEdit *>("captureDirectory")->setText(directory.path());
        auto *start = window.findChild<QPushButton *>("captureStart");
        auto *stop = window.findChild<QPushButton *>("captureStop");
        auto *status = window.findChild<QLabel *>("captureStatus");
        start->click();
        check(start->isEnabled() && !stop->isEnabled(), "missing metadata not rejected");
        window.findChild<QLineEdit *>("captureBatch")->setText("fixture_run_01");
        window.findChild<QLineEdit *>("capturePosition")->setText("fixture_mount");
        window.findChild<QSpinBox *>("captureDuration")->setValue(5);
        auto *condition = window.findChild<QComboBox *>("captureCondition");
        condition->setCurrentIndex(3);
        start->click();
        check(start->isEnabled(), "confirmed fault without evidence not rejected");
        condition->setCurrentIndex(0);
        for (const auto &scenario : {"source_reject", "malformed", "disconnect", "timeout", "normal"}) {
            mode = scenario;
            start->click();
            waitUntil([&] { return start->isEnabled(); });
            const QString expected = mode == "source_reject" ? "unverified_sensor_source"
                : mode == "malformed" ? "invalid_sample"
                : mode == "disconnect" ? "incomplete_response"
                : mode == "timeout" ? "gateway_timeout" : QStringLiteral("采集完成");
            check(status->text().contains(expected)
                || (mode == "disconnect" && status->text().contains("network_error")),
                "unexpected capture result");
            qInfo("PASS %s", scenario);
        }
        mode = "normal";
        start->click();
        waitUntil([&] {
            const auto dirs = QDir(directory.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            if (dirs.isEmpty()) return false;
            QFile csv(QDir(directory.path()).filePath(dirs.last() + "/samples.csv"));
            return csv.size() > 500;
        });
        stop->click();
        check(start->isEnabled() && status->text().contains(QStringLiteral("已停止")), "stop failed");
        const auto dirs = QDir(directory.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        check(dirs.size() == 6, "session directories not unique");
        bool completedFound = false;
        for (const QString &dir : dirs) {
            QFile meta(QDir(directory.path()).filePath(dir + "/metadata.json"));
            check(meta.open(QIODevice::ReadOnly), "metadata absent");
            const auto object = QJsonDocument::fromJson(meta.readAll()).object();
            check(object.value("continuous_sampling_verified") == false, "false continuity claim");
            check(object.value("experiment_batch") == "fixture_run_01", "batch missing");
            QFile csv(QDir(directory.path()).filePath(dir + "/samples.csv"));
            check(csv.open(QIODevice::ReadOnly), "csv absent");
            const auto lines = csv.readAll().split('\n');
            check(lines.size() - 2 == object.value("samples").toInt(), "csv count mismatch");
            if (object.value("status") == "completed") {
                completedFound = true;
                check(object.value("samples").toInt() >= 256, "no completed samples");
                check(lines[1].startsWith("0,0,-128,"), "raw data not preserved");
            }
        }
        check(completedFound, "completion metadata missing");
        window.findChild<QLineEdit *>("captureDirectory")->setText(directory.filePath("captures"));
        window.show();
        QApplication::processEvents();
        check(window.grab().save("capture-preview.png"), "screenshot failed");
        window.resize(560, 620);
        QApplication::processEvents();
        check(window.grab().save("capture-preview-compact.png"), "compact screenshot failed");
        qInfo("PASS stop, metadata, CSV, unique sessions, screenshots");
        return 0;
    } catch (const std::exception &e) {
        qCritical("FAIL %s", e.what());
        return 1;
    }
}
