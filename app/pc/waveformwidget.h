#pragma once

#include <QColor>
#include <QVector>
#include <QWidget>

// 用于振动、电流和温度的滚动波形
class WaveformWidget : public QWidget
{
    Q_OBJECT

public:
    explicit WaveformWidget(QWidget *parent = nullptr);

    void setTitle(const QString &title);
    void setUnit(const QString &unit);
    void setLineColor(const QColor &color);
    void appendSample(double value);
    void replaceSamples(const QVector<double> &samples);
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString title_;
    QString unit_;
    QColor lineColor_;
    QVector<double> samples_;
    static constexpr int kMaxSamples = 360;
};
