#pragma once

#include <QWidget>
#include <QString>
#include <QUrl>

class QQuickView;
class QQuickItem;

// jichu 五部件 glb + Qt Quick 3D（QQuickView 嵌入，避免 QQuickWidget 黑屏）
class MotorView3D : public QWidget
{
    Q_OBJECT

public:
    explicit MotorView3D(QWidget *parent = nullptr);
    ~MotorView3D() override;

    void setTwinVisual(int deviceId, const QString &runState, const QString &faultSite);
    void setMotorRuntime(int deviceId, bool running, int pwm, int direction);
    /** 仅重播「整台→拆解」；勿在 HUD 定时刷新里调用 */
    void replayExplodeAnimation();
    bool isReady() const { return qmlReady_; }
    QString loadError() const { return loadError_; }
    static QString resolvedMeshesDir();
    static QString resolvedCombinedMeshPath();
    static QString resolvedStationMeshesDir();

private:
    static QUrl resolveMeshesBaseUrl();
    static QUrl resolveCombinedMeshUrl();
    static QUrl resolveStationMeshesBaseUrl();
    void applyToQml(const QString &runState, const QString &faultSite);

    QQuickView *view_ = nullptr;
    QWidget *container_ = nullptr;
    bool qmlReady_ = false;
    QString loadError_;
    int deviceId_ = 3;
    int animSeq_ = 0;
    QString runState_;
    QString faultSite_;
    int motorDeviceId_ = 1;
    bool motorRunning_ = false;
    int motorPwm_ = 0;
    int motorDirection_ = 0;
};
