#pragma once

#include <QWidget>

// 右侧双视图组件：显示故障部位与设备运行状态
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
