#include "motorview3d.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QQuickItem>
#include <QQuickView>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QVBoxLayout>

namespace {

void ensureQtQuick3DRuntimePaths()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    const QString appDir = QCoreApplication::applicationDirPath();
    QCoreApplication::addLibraryPath(appDir);
    QCoreApplication::addLibraryPath(appDir + QStringLiteral("/plugins"));
    QCoreApplication::addLibraryPath(appDir + QStringLiteral("/qml"));
    QCoreApplication::addLibraryPath(QLibraryInfo::path(QLibraryInfo::PluginsPath));
}

QUrl resolveMotorSceneQmlUrl()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(QStringLiteral("qml/MotorScene.qml")),
        QDir(appDir).filePath(QStringLiteral("../qml/MotorScene.qml")),
        QDir(appDir).filePath(QStringLiteral("../../qml/MotorScene.qml")),
    };
    for (const QString &path : candidates) {
        if (QFileInfo::exists(path))
            return QUrl::fromLocalFile(QDir::toNativeSeparators(path));
    }
    return QUrl(QStringLiteral("qrc:/qml/MotorScene.qml"));
}

} // namespace

MotorView3D::MotorView3D(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(480, 360);
    ensureQtQuick3DRuntimePaths();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    view_ = new QQuickView();
    view_->setColor(QColor(0x0a, 0x10, 0x1a));
    view_->setResizeMode(QQuickView::SizeRootObjectToView);
    const QString appDir = QCoreApplication::applicationDirPath();
    view_->engine()->addImportPath(QStringLiteral("qrc:/"));
    view_->engine()->addImportPath(appDir);
    view_->engine()->addImportPath(appDir + QStringLiteral("/qml"));

    QQmlContext *ctx = view_->rootContext();
    ctx->setContextProperty(QStringLiteral("meshBaseUrl"), resolveMeshesBaseUrl());
    ctx->setContextProperty(QStringLiteral("combinedMeshUrl"), resolveCombinedMeshUrl());
    ctx->setContextProperty(QStringLiteral("stationMeshBaseUrl"), resolveStationMeshesBaseUrl());

    const QUrl sceneUrl = resolveMotorSceneQmlUrl();
    qInfo().noquote() << "MotorView3D QML:" << sceneUrl.toString();
    view_->setSource(sceneUrl);

    if (view_->status() == QQuickView::Error) {
        QStringList lines;
        for (const QQmlError &err : view_->errors()) {
            const QString s = err.toString();
            lines << s;
            qWarning("MotorView3D QML: %s", qPrintable(s));
        }
        loadError_ = lines.isEmpty()
                         ? QStringLiteral("Qt Quick3D QML 加载失败")
                         : lines.join(QStringLiteral("\n"));
        delete view_;
        view_ = nullptr;
        return;
    }

    qInfo().noquote() << "MotorView3D meshes:" << resolveMeshesBaseUrl().toString()
                      << "combined:" << resolveCombinedMeshUrl().toString()
                      << "stations:" << resolveStationMeshesBaseUrl().toString();

    container_ = QWidget::createWindowContainer(view_, this);
    container_->setFocusPolicy(Qt::StrongFocus);
    layout->addWidget(container_, 1);

    qmlReady_ = true;
    applyToQml(QStringLiteral("normal"), QStringLiteral("none"));
}

MotorView3D::~MotorView3D()
{
    if (view_) {
        container_ = nullptr;
        delete view_;
        view_ = nullptr;
    }
}

QUrl MotorView3D::resolveMeshesBaseUrl()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(QStringLiteral("assets/parts/meshes/")),
        QDir(appDir).filePath(QStringLiteral("../assets/parts/meshes/")),
        QDir(appDir).filePath(QStringLiteral("../../assets/parts/meshes/")),
    };
    for (const QString &dirPath : candidates) {
        const QDir dir(dirPath);
        if (dir.exists(QStringLiteral("body.mesh")))
            return QUrl::fromLocalFile(QDir::toNativeSeparators(dir.absolutePath()) + QDir::separator());
    }
    return QUrl();
}

QString MotorView3D::resolvedMeshesDir()
{
    const QUrl u = resolveMeshesBaseUrl();
    return u.isLocalFile() ? u.toLocalFile() : QString();
}

QString MotorView3D::resolvedCombinedMeshPath()
{
    const QUrl u = resolveCombinedMeshUrl();
    return u.isLocalFile() ? u.toLocalFile() : QString();
}

QString MotorView3D::resolvedStationMeshesDir()
{
    const QUrl u = resolveStationMeshesBaseUrl();
    return u.isLocalFile() ? u.toLocalFile() : QString();
}

QUrl MotorView3D::resolveCombinedMeshUrl()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(QStringLiteral("assets/parts/meshes/motor_assembled.mesh")),
        QDir(appDir).filePath(QStringLiteral("../assets/parts/meshes/motor_assembled.mesh")),
        QDir(appDir).filePath(QStringLiteral("../../assets/parts/meshes/motor_assembled.mesh")),
        QDir(appDir).filePath(QStringLiteral("assets/parts/meshes/motor_jichu_5parts.mesh")),
    };
    for (const QString &path : candidates) {
        if (QFileInfo::exists(path))
            return QUrl::fromLocalFile(QDir::toNativeSeparators(path));
    }
    return QUrl();
}

QUrl MotorView3D::resolveStationMeshesBaseUrl()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(QStringLiteral("assets/stations/meshes/")),
        QDir(appDir).filePath(QStringLiteral("../assets/stations/meshes/")),
        QDir(appDir).filePath(QStringLiteral("../../assets/stations/meshes/")),
    };
    for (const QString &dirPath : candidates) {
        const QDir dir(dirPath);
        if (dir.exists(QStringLiteral("load_pump/Load_pump.qml")))
            return QUrl::fromLocalFile(QDir::toNativeSeparators(dir.absolutePath()) + QDir::separator());
    }
    return QUrl();
}

void MotorView3D::applyToQml(const QString &runState, const QString &faultSite)
{
    runState_ = runState;
    faultSite_ = faultSite;
    if (!qmlReady_ || !view_)
        return;
    QQuickItem *root = view_->rootObject();
    if (!root)
        return;
    root->setProperty("runState", runState_);
    root->setProperty("faultSite", faultSite_);
    root->setProperty("animSeq", animSeq_);
    root->setProperty("deviceId", deviceId_);
    const bool showMotorRuntime = deviceId_ == motorDeviceId_;
    root->setProperty("motorRunning", showMotorRuntime && motorRunning_);
    root->setProperty("motorPwm", showMotorRuntime ? motorPwm_ : 0);
    root->setProperty("motorDirection",
                      showMotorRuntime ? motorDirection_ : 0);
}

void MotorView3D::setTwinVisual(int deviceId, const QString &runState, const QString &faultSite)
{
    deviceId_ = deviceId;
    applyToQml(runState, faultSite);
}

void MotorView3D::setMotorRuntime(
    int deviceId, bool running, int pwm, int direction)
{
    motorDeviceId_ = deviceId;
    motorRunning_ = running;
    motorPwm_ = qBound(0, pwm, 1000);
    motorDirection_ = direction == 0 ? 0 : 1;
    applyToQml(runState_, faultSite_);
}

void MotorView3D::replayExplodeAnimation()
{
    ++animSeq_;
    applyToQml(runState_, faultSite_);
}
