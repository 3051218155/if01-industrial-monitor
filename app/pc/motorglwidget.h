#pragma once

#include <QOpenGLWidget>
#include <QTimer>
#include <QString>

// 右侧 OpenGL 电机简模：按运行状态 / 故障部位变色。
class MotorGlWidget : public QOpenGLWidget
{
    Q_OBJECT

public:
    explicit MotorGlWidget(QWidget *parent = nullptr);

    void setTwinVisual(int deviceId, const QString &runState, const QString &faultSite);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    int deviceId_ = 3;
    QString runState_ = QStringLiteral("normal");
    QString faultSite_ = QStringLiteral("none");
    float spin_ = 0.f;
    QTimer spinTimer_;
};
