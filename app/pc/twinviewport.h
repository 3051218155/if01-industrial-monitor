#pragma once

#include <QWidget>

// 右侧 3D 视口占位：后续可替换为 OpenGL / 导入模型
class TwinViewportWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TwinViewportWidget(QWidget *parent = nullptr);

    void setFaultSite(const QString &site);
    void setRunState(const QString &state);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString faultSite_;
    QString runState_;
};
