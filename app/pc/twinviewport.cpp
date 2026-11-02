#include "twinviewport.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

TwinViewportWidget::TwinViewportWidget(QWidget *parent)
    : QWidget(parent)
    , faultSite_(QStringLiteral("none"))
    , runState_(QStringLiteral("normal"))
{
    setMinimumSize(480, 360);
}

void TwinViewportWidget::setFaultSite(const QString &site)
{
    faultSite_ = site;
    update();
}

void TwinViewportWidget::setRunState(const QString &state)
{
    runState_ = state;
    update();
}

void TwinViewportWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect r = rect();

    // 背景渐变
    QLinearGradient bg(0, 0, r.width(), r.height());
    bg.setColorAt(0.0, QColor(0x0a, 0x0e, 0x17));
    bg.setColorAt(0.55, QColor(0x0f, 0x16, 0x28));
    bg.setColorAt(1.0, QColor(0x08, 0x0c, 0x14));
    p.fillRect(r, bg);

    // 网格地面（2D 透视示意，3D 模型导入前保持「孪生感」）
    p.setPen(QPen(QColor(0x2a, 0x35, 0x50, 180), 1));
    const int horizonY = r.height() * 58 / 100;
    const QPoint center(r.width() / 2, horizonY);

    for (int i = -8; i <= 8; ++i) {
        p.drawLine(center, QPoint(center.x() + i * 42, r.height() - 12));
    }
    for (int j = 0; j < 7; ++j) {
        const int y = horizonY + j * 28;
        const int spread = 60 + j * 36;
        p.drawLine(center.x() - spread, y, center.x() + spread, y);
    }

    // 中心占位圆环（模型挂载点）
    p.setPen(QPen(QColor(0x50, 0x8c, 0xff, 160), 2));
    p.setBrush(QColor(0x12, 0x18, 0x26, 200));
    p.drawEllipse(center, 88, 88);
    p.setPen(QPen(QColor(0x7e, 0xb8, 0xff, 100), 1, Qt::DashLine));
    p.drawEllipse(center, 120, 120);

    // 主提示文字
    p.setPen(QColor(0xe8, 0xee, 0xf7));
    QFont titleFont = font();
    titleFont.setPointSize(18);
    titleFont.setBold(true);
    p.setFont(titleFont);
    p.drawText(r.adjusted(0, center.y() - 16, 0, 0), Qt::AlignHCenter | Qt::AlignTop,
               QStringLiteral("3D 数字孪生视口"));

    QFont subFont = font();
    subFont.setPointSize(11);
    p.setFont(subFont);
    p.setPen(QColor(0x88, 0x99, 0xaa));
    p.drawText(r.adjusted(0, center.y() + 12, 0, 0), Qt::AlignHCenter | Qt::AlignTop,
               QStringLiteral("OpenGL / 外部模型 · 稍后导入"));

    // 故障高亮提示（与左侧数据联动）
    QString siteText;
    if (faultSite_ == QLatin1String("bearing"))
        siteText = QStringLiteral("高亮部位：轴承端");
    else if (faultSite_ == QLatin1String("imbalance"))
        siteText = QStringLiteral("高亮部位：转子不平衡");
    else if (faultSite_ == QLatin1String("looseness"))
        siteText = QStringLiteral("高亮部位：地脚松动");
    else if (faultSite_ == QLatin1String("coupling"))
        siteText = QStringLiteral("高亮部位：联轴器");
    else if (faultSite_ == QLatin1String("body"))
        siteText = QStringLiteral("高亮部位：机壳");
    else
        siteText = QStringLiteral("高亮部位：无");

    QColor badgeBg(0x2e, 0xa0, 0x5a, 70);
    QColor badgeFg(0x6e, 0xe7, 0xa0);
    if (runState_ == QLatin1String("warning")) {
        badgeBg = QColor(0xdc, 0xa0, 0x28, 70);
        badgeFg = QColor(0xfc, 0xd3, 0x4d);
    } else if (runState_ == QLatin1String("fault")) {
        badgeBg = QColor(0xdc, 0x3c, 0x3c, 90);
        badgeFg = QColor(0xfc, 0xa5, 0xa5);
    }

    const QString badge = siteText;
    QFontMetrics fm(subFont);
    const int badgeW = fm.horizontalAdvance(badge) + 28;
    const int badgeH = 32;
    const QRect badgeRect(20, 20, badgeW, badgeH);

    p.setPen(Qt::NoPen);
    p.setBrush(badgeBg);
    p.drawRoundedRect(badgeRect, 8, 8);
    p.setPen(badgeFg);
    p.drawText(badgeRect, Qt::AlignCenter, badge);

    // 图例
    p.setPen(QColor(0x66, 0x77, 0x88));
    p.setFont(subFont);
    const int legendY = r.height() - 28;
    auto drawLegend = [&](int x, QColor c, const QString &text) {
        p.setBrush(c);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(x, legendY, 10, 10, 2, 2);
        p.setPen(QColor(0x88, 0x99, 0xaa));
        p.drawText(x + 16, legendY + 10, text);
    };
    drawLegend(20, QColor(0x6b, 0x72, 0x80), QStringLiteral("正常"));
    drawLegend(90, QColor(0xea, 0xb3, 0x08), QStringLiteral("预警"));
    drawLegend(160, QColor(0xef, 0x44, 0x44), QStringLiteral("故障高亮"));
}
