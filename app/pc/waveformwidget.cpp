#include "waveformwidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <algorithm>

WaveformWidget::WaveformWidget(QWidget *parent)
    : QWidget(parent)
    , title_(QStringLiteral("波形"))
    , unit_(QString())
    , lineColor_(QColor(0x3b, 0xea, 0xbf))
{
    setMinimumHeight(88);
    samples_.fill(0.0, kMaxSamples);
}

void WaveformWidget::setTitle(const QString &title)
{
    title_ = title;
    update();
}

void WaveformWidget::setUnit(const QString &unit)
{
    unit_ = unit;
    update();
}

void WaveformWidget::setLineColor(const QColor &color)
{
    lineColor_ = color;
    update();
}

void WaveformWidget::appendSample(double value)
{
    samples_.removeFirst();
    samples_.append(value);
    update();
}

void WaveformWidget::replaceSamples(const QVector<double> &samples)
{
    if (samples.isEmpty())
        return;
    samples_ = samples;
    update();
}

void WaveformWidget::clear()
{
    samples_.fill(0.0, kMaxSamples);
    update();
}

void WaveformWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect r = rect().adjusted(1, 1, -1, -1);

    p.fillRect(r, QColor(0x0a, 0x10, 0x1a));

    p.setPen(QPen(QColor(0x24, 0x30, 0x47), 1));
    p.drawRoundedRect(r, 8, 8);

    p.setPen(QPen(QColor(0x1e, 0x29, 0x3b), 1));
    for (int x = r.left() + 20; x < r.right(); x += 24) {
        p.drawLine(x, r.top() + 22, x, r.bottom() - 8);
    }
    for (int y = r.top() + 22; y < r.bottom(); y += 18) {
        p.drawLine(r.left() + 4, y, r.right() - 4, y);
    }

    p.setPen(QColor(0x88, 0x99, 0xaa));
    QFont titleFont = font();
    titleFont.setPointSize(9);
    p.setFont(titleFont);
    p.drawText(r.adjusted(10, 6, -8, 0), Qt::AlignLeft | Qt::AlignTop, title_);

    const int plotTop = r.top() + 24;
    const int plotBottom = r.bottom() - 10;
    const int plotH = qMax(1, plotBottom - plotTop);
    const int plotW = qMax(1, r.width() - 12);
    const double midY = plotTop + plotH * 0.5;

    // 自动量程：让波形始终占满约 80% 高度（否则小信号看起来像直线）
    double maxAbs = 0.0;
    for (double v : samples_)
        maxAbs = std::max(maxAbs, std::fabs(v));
    // 下限防止把噪声放大成"大波浪"
    const double scale = std::max({maxAbs * 1.25, 1e-6});

    p.setPen(QPen(QColor(0x33, 0x41, 0x55), 1, Qt::DotLine));
    p.drawLine(r.left() + 6, int(midY), r.right() - 6, int(midY));

    QPainterPath path;
    for (int i = 0; i < samples_.size(); ++i) {
        const double x = r.left() + 6 + (plotW * i) / qMax(1, samples_.size() - 1);
        const double y = midY - (samples_.at(i) / scale) * plotH * 0.42;
        if (i == 0)
            path.moveTo(x, y);
        else
            path.lineTo(x, y);
    }

    p.setPen(QPen(lineColor_, 2));
    p.drawPath(path);

    if (!unit_.isEmpty()) {
        QFont vf = font();
        vf.setPointSize(8);
        p.setFont(vf);
        p.setPen(QColor(0x64, 0x74, 0x8b));
        p.drawText(r.adjusted(0, 6, -10, 0), Qt::AlignRight | Qt::AlignTop,
                   QStringLiteral("±%1 %2").arg(scale, 0, 'f', scale < 1 ? 3 : 2).arg(unit_));
    }

    p.setPen(QPen(QColor(lineColor_.red(), lineColor_.green(), lineColor_.blue(), 90), 1, Qt::DashLine));
    const int sweepX = r.right() - 8;
    p.drawLine(sweepX, plotTop, sweepX, plotBottom);

    if (!samples_.isEmpty()) {
        const double lastY = midY - (samples_.last() / scale) * plotH * 0.42;
        p.setPen(Qt::NoPen);
        p.setBrush(lineColor_);
        p.drawEllipse(QPointF(sweepX - 1, lastY), 3, 3);
    }
}
