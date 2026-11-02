#include "mainwindow.h"
#include "capturewindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QLockFile>
#include <QMessageBox>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

#ifdef Q_OS_WIN
bool stopPreviousInstance(QLockFile &lock)
{
    qint64 pid = 0;
    QString hostName;
    QString appName;
    if (!lock.getLockInfo(&pid, &hostName, &appName) || pid <= 0
        || pid == QCoreApplication::applicationPid()) {
        return false;
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE
                                     | SYNCHRONIZE,
                                 FALSE, static_cast<DWORD>(pid));
    if (!process)
        return false;

    wchar_t path[32768];
    DWORD length = DWORD(sizeof(path) / sizeof(path[0]));
    const bool sameExecutable =
        QueryFullProcessImageNameW(process, 0, path, &length)
        && QString::compare(QDir::cleanPath(QString::fromWCharArray(path, length)),
                            QDir::cleanPath(QCoreApplication::applicationFilePath()),
                            Qt::CaseInsensitive) == 0;
    const bool stopped = sameExecutable && TerminateProcess(process, 0)
        && WaitForSingleObject(process, 5000) == WAIT_OBJECT_0;
    CloseHandle(process);
    return stopped;
}
#endif

void prependExeRelativeQtPaths()
{
    const QString appDir = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
    QCoreApplication::addLibraryPath(appDir);
    QCoreApplication::addLibraryPath(appDir + QStringLiteral("/plugins"));
    QCoreApplication::addLibraryPath(appDir + QStringLiteral("/qml"));
}

} // namespace

int main(int argc, char *argv[])
{
    // Quick3D 在 Widget 里用 OpenGL RHI；勿强制 Core 3.3，否则 glb 可能加载失败
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    QApplication app(argc, argv);
    prependExeRelativeQtPaths();

    QLockFile instanceLock(
        QDir::temp().filePath(QStringLiteral("fpga_learn_display.lock")));
    bool locked = instanceLock.tryLock(0);
#ifdef Q_OS_WIN
    if (!locked && stopPreviousInstance(instanceLock))
        locked = instanceLock.tryLock(5000);
#endif
    if (!locked) {
        QMessageBox::information(
            nullptr,
            QStringLiteral("工业孪生对话网关"),
            QStringLiteral("无法接管 Qt 大屏实例，请检查其他版本是否正在运行。"));
        return 0;
    }

    if (app.arguments().contains(QStringLiteral("--capture"))) {
        CaptureWindow capture;
        capture.show();
        return app.exec();
    }

    MainWindow w;
    w.show();

    if (app.arguments().contains(QStringLiteral("--report"))) {
        qInfo("Test report auto-open requested");
        QTimer::singleShot(1500, &w, [&w] {
            w.showTestReport();
        });
    }

    return app.exec();
}
