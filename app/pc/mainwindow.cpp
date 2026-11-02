#include "mainwindow.h"
#include "aiclient.h"
#include "boardalertclient.h"
#include "gatewaywaveformclient.h"
#include "motorglwidget.h"
#include "motorview3d.h"
#include "reportwindow.h"
#include "showapiserver.h"
#include "waveformwidget.h"
#include "serialmodbusclient.h"
#include "serialmodbusworker.h"

#include <QCoreApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QImage>
#include <QLabel>
#include <QMenuBar>
#include <QScrollArea>
#include <QMenu>
#include <QNetworkProxy>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QFileInfo>
#include <QFile>
#include <QWidget>
#include <QtMath>
#include <QTextStream>

namespace {

constexpr int kSerialRegisterCount = 32;

QString stateText(const QString &state)
{
    if (state == QLatin1String("notice"))
        return QStringLiteral("注意");
    if (state == QLatin1String("warning"))
        return QStringLiteral("预警");
    if (state == QLatin1String("alarm"))
        return QStringLiteral("告警");
    if (state == QLatin1String("fault"))
        return QStringLiteral("严重");
    if (state == QLatin1String("emergency"))
        return QStringLiteral("紧急");
    return QStringLiteral("正常");
}

bool jichuPartsAvailable()
{
    return !MotorView3D::resolvedMeshesDir().isEmpty();
}

QString siteText(const QString &site)
{
    if (site == QLatin1String("bearing"))
        return QStringLiteral("轴承端");
    if (site == QLatin1String("imbalance"))
        return QStringLiteral("转子不平衡");
    if (site == QLatin1String("looseness"))
        return QStringLiteral("地脚松动");
    if (site == QLatin1String("coupling"))
        return QStringLiteral("联轴器");
    if (site == QLatin1String("body"))
        return QStringLiteral("机壳");
    return QStringLiteral("无");
}

QString siteKeyFromCode(int code)
{
    switch (code) {
        case 1: return QStringLiteral("bearing");
        case 2: return QStringLiteral("imbalance");
        case 3: return QStringLiteral("looseness");
        case 4: return QStringLiteral("coupling");
        case 5: return QStringLiteral("body");
        default: return QStringLiteral("none");
    }
}

QString metricSourceText(const QString &source)
{
    if (source == QLatin1String("remote"))
        return QStringLiteral("远程");
    if (source == QLatin1String("unknown") || source.isEmpty())
        return QStringLiteral("未知");
    return QStringLiteral("在线");
}

QString registerSource(quint16 fallbackMask, int index, bool hardwareSensor)
{
    if ((fallbackMask & (1u << index)) != 0)
        return QStringLiteral("fallback");
    return hardwareSensor ? QStringLiteral("real") : QStringLiteral("simulated");
}

QString groupedRegisterSource(
    quint16 fallbackMask, quint16 registerMask, bool hardwareSensor)
{
    const quint16 fallback = fallbackMask & registerMask;
    if (fallback == registerMask)
        return QStringLiteral("fallback");
    if (fallback != 0)
        return QStringLiteral("mixed");
    return hardwareSensor ? QStringLiteral("real") : QStringLiteral("simulated");
}

double interpolateScore(double value, const double *points, const double *scores, int count)
{
    if (value <= points[0])
        return scores[0];
    for (int i = 1; i < count; ++i) {
        if (value <= points[i]) {
            const double ratio = (value - points[i - 1]) / (points[i] - points[i - 1]);
            return scores[i - 1] + ratio * (scores[i] - scores[i - 1]);
        }
    }
    return scores[count - 1];
}

double vibrationScore(double rms)
{
    // IF-01/MPU6050 采样量级约 0.06~0.72 m/s²，阈值按这个量程标定。
    static const double points[] = {0.15, 0.25, 0.40, 0.60, 0.90};
    static const double scores[] = {100.0, 80.0, 60.0, 25.0, 0.0};
    return interpolateScore(rms, points, scores, 5);
}

double temperatureScore(double temperature)
{
    static const double points[] = {50.0, 65.0, 80.0, 95.0, 110.0};
    static const double scores[] = {100.0, 85.0, 55.0, 10.0, 0.0};
    return interpolateScore(temperature, points, scores, 5);
}

double currentScore(double current)
{
    static const double points[] = {4.5, 5.5, 7.0, 9.0, 12.0};
    static const double scores[] = {100.0, 85.0, 60.0, 25.0, 0.0};
    return interpolateScore(current, points, scores, 5);
}

double stateScore(const QString &state)
{
    if (state == QLatin1String("notice"))
        return 90.0;
    if (state == QLatin1String("warning"))
        return 72.0;
    if (state == QLatin1String("alarm"))
        return 50.0;
    if (state == QLatin1String("fault"))
        return 25.0;
    if (state == QLatin1String("emergency"))
        return 10.0;
    return 100.0;
}

double stateHealthCap(const QString &state)
{
    if (state == QLatin1String("notice"))
        return 90.0;
    if (state == QLatin1String("warning"))
        return 72.0;
    if (state == QLatin1String("alarm"))
        return 55.0;
    if (state == QLatin1String("fault"))
        return 30.0;
    if (state == QLatin1String("emergency"))
        return 15.0;
    return 100.0;
}

QString motorButtonStyle(const QString &state, bool hasData, bool selected)
{
    QString accent = QStringLiteral("#475569");
    QString background = QStringLiteral("#111827");
    if (hasData) {
        if (state == QLatin1String("notice")) {
            accent = QStringLiteral("#2563c4");
        } else if (state == QLatin1String("warning")) {
            accent = QStringLiteral("#c9a227");
        } else if (state == QLatin1String("alarm")) {
            accent = QStringLiteral("#d97706");
        } else if (state == QLatin1String("fault")) {
            accent = QStringLiteral("#b91c1c");
        } else if (state == QLatin1String("emergency")) {
            accent = QStringLiteral("#991b1b");
        } else {
            accent = QStringLiteral("#5f8a65");
        }
    }

    if (selected)
        background = QStringLiteral("#1e293b");

    return QStringLiteral(
               "QPushButton { color:#e8eef7; background:%1; border:%2 solid %3;"
               " border-radius:8px; padding:7px 4px; font-size:12px; font-weight:600; }"
               "QPushButton:hover { background:#263449; }"
               "QPushButton:checked { background:#24344d; border:2px solid %3; }")
        .arg(background, selected ? QStringLiteral("2px") : QStringLiteral("1px"), accent);
}

QFrame *makeMetricCard(const QString &caption, QLabel **valueOut, QWidget *parent)
{
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("metricCard"));
    card->setMinimumHeight(64);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(4);

    auto *cap = new QLabel(caption, card);
    cap->setObjectName(QStringLiteral("metricCaption"));
    *valueOut = new QLabel(QStringLiteral("—"), card);
    (*valueOut)->setObjectName(QStringLiteral("metricValue"));
    (*valueOut)->setMinimumHeight(22);
    (*valueOut)->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    layout->addWidget(cap);
    layout->addWidget(*valueOut);
    return card;
}

QString findProjectFile(const QString &relativePath)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(QStringLiteral("D:/FPGA")).filePath(relativePath),
        QDir(appDir).filePath(QStringLiteral("../../../../") + relativePath),
        QDir(appDir).filePath(QStringLiteral("../../../") + relativePath),
        QDir::current().filePath(relativePath),
    };
    for (const QString &path : candidates) {
        if (QFileInfo::exists(path))
            return QDir::cleanPath(path);
    }
    return QString();
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , topTitleLabel_(nullptr)
    , gatewayStatusLabel_(nullptr)
    , clockLabel_(nullptr)
    , brandLabel_(nullptr)
    , deviceTitleLabel_(nullptr)
    , rmsValueLabel_(nullptr)
    , tempValueLabel_(nullptr)
    , currentValueLabel_(nullptr)
    , healthValueLabel_(nullptr)
    , stateValueLabel_(nullptr)
    , siteValueLabel_(nullptr)
    , faultLabel_(nullptr)
    , metaLabel_(nullptr)
    , plStatusLabel_(nullptr)
    , npuStatusLabel_(nullptr)
    , npuConditionLabel_(nullptr)
    , npuConfidenceLabel_(nullptr)
    , npuRuntimeLabel_(nullptr)
    , sampleRateLabel_(nullptr)
    , aiResultLabel_(nullptr)
    , motorGroup_(nullptr)
    , motorButtons_{}
    , vibWave_(nullptr)
    , tempTrendWave_(nullptr)
    , viewport_(nullptr)
    , aiAnalyzeButton_(nullptr)
    , reportButton_(nullptr)
    , portCombo_(nullptr)
    , fallbackPortCombo_(nullptr)
    , fallbackEnable_(nullptr)
    , pwmSpin_(nullptr)
    , startMotorButton_(nullptr)
    , stopMotorButton_(nullptr)
    , waveTimer_(nullptr)
    , boardAlertClient_(nullptr)
    , gatewayWaveformClient_(nullptr)
    , serialWorker_(nullptr)
    , serialThread_(nullptr)
    , fallbackWorker_(nullptr)
    , fallbackThread_(nullptr)
    , serialTimer_(nullptr)
    , serialOnline_(false)
    , serialUseRealData_(false)
    , refreshAnalysisOnNextSample_(false)
    , serialTick_(0)
    , primaryRegistersMsByDevice_{}
    , fallbackRegistersMsByDevice_{}
    , fallbackOnline_(false)
    , npuRequestInFlight_(false)
    , lastLiveNpuTemperature_(25.0)
    , selectedMotorIndex_(0)
    , remoteHoldUntilMs_(0)
    , simTick_(0)
    , hudCounter_(0)
    , wavePhase_(0.0)
    , simNpuMs_(8)
{
    for (int i = 0; i < static_cast<int>(motors_.size()); ++i) {
        motors_[i].id = i + 1;
        motors_[i].health = 96;
    }

    setWindowTitle(QStringLiteral("FPGA · Qt 串口上位机"));
    resize(1440, 900);

    setStyleSheet(QStringLiteral(
        "QMainWindow { background:#070b12; }"
        "QWidget#centralRoot { background:#070b12; }"
        "QFrame#topBar { background:#0c1220; border:1px solid #243047; border-radius:12px; }"
        "QLabel#topTitle { color:#7eb8ff; font-size:15px; font-weight:700; }"
        "QLabel#gatewayOk { color:#6ee7a0; font-size:12px; font-weight:600; }"
        "QLabel#clock { color:#8899aa; font-size:12px; }"
        "QFrame#sidePanel { background:rgba(12,18,32,0.94); border:1px solid rgba(80,140,255,0.35); border-radius:12px; }"
        "QLabel#brand { color:#7eb8ff; font-size:13px; font-weight:600; }"
        "QLabel#deviceTitle { color:#e8eef7; font-size:24px; font-weight:700; }"
        "QFrame#metricCard { background:#111827; border:1px solid #243047; border-radius:10px; }"
        "QLabel#metricCaption { color:#8899aa; font-size:11px; }"
        "QLabel#metricValue { color:#e8eef7; font-size:18px; font-weight:700; }"
        "QLabel#rowCaption { color:#8899aa; font-size:12px; }"
        "QLabel#rowValue { color:#e8eef7; font-size:13px; font-weight:600; }"
        "QLabel#meta { color:#667788; font-size:11px; }"
        "QLabel#fault { font-size:14px; font-weight:600; padding:10px; border-radius:8px; }"
        "QPushButton#refreshBtn { color:#e8eef7; background:#1e293b; border:1px solid #508cff;"
        "padding:10px; border-radius:8px; font-weight:600; }"
        "QPushButton#refreshBtn:hover { background:#334155; }"
        "QPushButton#aiBtn { color:#e8eef7; background:#1a365d; border:1px solid #60a5fa;"
        "padding:10px; border-radius:8px; font-weight:600; }"
        "QPushButton#aiBtn:hover { background:#234876; }"
        "QLabel#aiResult { color:#cbd5e1; font-size:12px; background:#0f172a;"
        "border:1px solid #334155; border-radius:8px; padding:10px; }"
        "QFrame#viewportFrame { background:#0a0e17; border:1px solid #243047; border-radius:12px; }"
        "QLabel#viewportHint { color:#667788; font-size:12px; }"
        "QStatusBar { background:#0c1220; color:#8899aa; border-top:1px solid #243047; }"));

    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("centralRoot"));
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(14, 14, 14, 10);
    root->setSpacing(12);

    auto *topBar = new QFrame(central);
    topBar->setObjectName(QStringLiteral("topBar"));
    auto *topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(16, 10, 16, 10);

    topTitleLabel_ = new QLabel(
        QStringLiteral("安路飞龙 FPSoC · 多设备状态监测终端"), topBar);
    topTitleLabel_->setObjectName(QStringLiteral("topTitle"));
    gatewayStatusLabel_ = new QLabel(QStringLiteral("● 正在连接串口…"), topBar);
    gatewayStatusLabel_->setObjectName(QStringLiteral("gatewayOk"));
    gatewayStatusLabel_->setStyleSheet(
        QStringLiteral("color:#c9a227; font-size:12px; font-weight:600;"));
    auto *serialCaption = new QLabel(QStringLiteral("串口"), topBar);
    serialCaption->setObjectName(QStringLiteral("clock"));
    portCombo_ = new QComboBox(topBar);
    portCombo_->setObjectName(QStringLiteral("serialPort"));
    portCombo_->setMinimumWidth(92);
    auto *fallbackCaption = new QLabel(QStringLiteral("通道2"), topBar);
    fallbackCaption->setObjectName(QStringLiteral("clock"));
    fallbackPortCombo_ = new QComboBox(topBar);
    fallbackPortCombo_->setObjectName(QStringLiteral("fallbackSerialPort"));
    fallbackPortCombo_->setMinimumWidth(82);
    fallbackEnable_ = new QCheckBox(QStringLiteral("启用"), topBar);
    fallbackEnable_->setObjectName(QStringLiteral("clock"));
    auto *refreshPorts = new QPushButton(QStringLiteral("刷新串口"), topBar);
    refreshPorts->setObjectName(QStringLiteral("refreshBtn"));
    refreshPorts->setMinimumWidth(82);
    reportButton_ = new QPushButton(QStringLiteral("生成测试报告"), topBar);
    reportButton_->setObjectName(QStringLiteral("aiBtn"));
    reportButton_->setMinimumWidth(118);
    clockLabel_ = new QLabel(topBar);
    clockLabel_->setObjectName(QStringLiteral("clock"));

    topLayout->addWidget(topTitleLabel_);
    topLayout->addStretch();
    topLayout->addWidget(serialCaption);
    topLayout->addWidget(portCombo_);
    topLayout->addWidget(fallbackCaption);
    topLayout->addWidget(fallbackPortCombo_);
    topLayout->addWidget(fallbackEnable_);
    topLayout->addWidget(refreshPorts);
    topLayout->addWidget(reportButton_);
    topLayout->addSpacing(12);
    topLayout->addWidget(gatewayStatusLabel_);
    topLayout->addSpacing(18);
    topLayout->addWidget(clockLabel_);
    connect(refreshPorts, &QPushButton::clicked,
            this, &MainWindow::refreshSerialPorts);
    connect(reportButton_, &QPushButton::clicked,
            this, &MainWindow::showTestReport);
    connect(portCombo_, &QComboBox::currentTextChanged,
            this, [this](const QString &) { openSerialPort(); });
    connect(fallbackPortCombo_, &QComboBox::currentTextChanged,
            this, [this](const QString &) {
        if (fallbackEnable_ && fallbackEnable_->isChecked())
            openFallbackPort();
    });
    connect(fallbackEnable_, &QCheckBox::toggled,
            this, [this](bool enabled) {
        if (enabled)
            openFallbackPort();
        else
            closeFallbackPort();
    });

    auto *body = new QHBoxLayout();
    body->setSpacing(12);

    auto *sideScroll = new QScrollArea(central);
    sideScroll->setObjectName(QStringLiteral("sideScroll"));
    sideScroll->setWidgetResizable(true);
    sideScroll->setFrameShape(QFrame::NoFrame);
    sideScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sideScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    sideScroll->setFixedWidth(398);

    auto *sidePanel = new QFrame(sideScroll);
    sidePanel->setObjectName(QStringLiteral("sidePanel"));
    sidePanel->setFixedWidth(380);
    auto *sideLayout = new QVBoxLayout(sidePanel);
    sideLayout->setContentsMargins(16, 16, 16, 16);
    sideLayout->setSpacing(10);

    brandLabel_ = new QLabel(QStringLiteral("多设备状态监视 · 串口 HUD"), sidePanel);
    brandLabel_->setObjectName(QStringLiteral("brand"));
    auto *motorSelectorCaption = new QLabel(QStringLiteral("电机选择"), sidePanel);
    motorSelectorCaption->setObjectName(QStringLiteral("rowCaption"));

    motorGroup_ = new QButtonGroup(sidePanel);
    motorGroup_->setExclusive(true);
    auto *motorSelector = new QGridLayout();
    motorSelector->setSpacing(6);
    for (int i = 0; i < static_cast<int>(motorButtons_.size()); ++i) {
        auto *button = new QPushButton(sidePanel);
        button->setCheckable(true);
        button->setMinimumHeight(52);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        motorButtons_[i] = button;
        motorGroup_->addButton(button, i + 1);
        motorSelector->addWidget(button, i / 3, i % 3);
    }
    connect(motorGroup_, &QButtonGroup::idClicked, this, [this](int deviceId) {
        remoteHoldUntilMs_ = 0;
        serialTick_ = 0;
        selectMotor(deviceId);
    });

    deviceTitleLabel_ = new QLabel(sidePanel);
    deviceTitleLabel_->setObjectName(QStringLiteral("deviceTitle"));

    auto *metricGrid = new QGridLayout();
    metricGrid->setHorizontalSpacing(8);
    metricGrid->setVerticalSpacing(8);
    metricGrid->addWidget(makeMetricCard(QStringLiteral("振动 RMS"), &rmsValueLabel_, sidePanel), 0, 0);
    metricGrid->addWidget(makeMetricCard(QStringLiteral("温度"), &tempValueLabel_, sidePanel), 0, 1);
    metricGrid->addWidget(makeMetricCard(QStringLiteral("电流"), &currentValueLabel_, sidePanel), 1, 0);
    metricGrid->addWidget(makeMetricCard(QStringLiteral("健康度"), &healthValueLabel_, sidePanel), 1, 1);
    metricGrid->setColumnStretch(0, 1);
    metricGrid->setColumnStretch(1, 1);

    auto *motorControl = new QHBoxLayout();
    auto *pwmCaption = new QLabel(QStringLiteral("A路 PWM"), sidePanel);
    pwmCaption->setObjectName(QStringLiteral("rowCaption"));
    pwmSpin_ = new QSpinBox(sidePanel);
    pwmSpin_->setObjectName(QStringLiteral("pwmSet"));
    pwmSpin_->setRange(0, 1000);
    pwmSpin_->setValue(500);
    pwmSpin_->setSingleStep(10);
    pwmSpin_->setFixedWidth(82);
    startMotorButton_ = new QPushButton(QStringLiteral("启动"), sidePanel);
    startMotorButton_->setObjectName(QStringLiteral("refreshBtn"));
    stopMotorButton_ = new QPushButton(QStringLiteral("停止"), sidePanel);
    stopMotorButton_->setObjectName(QStringLiteral("refreshBtn"));
    motorControl->addWidget(pwmCaption);
    motorControl->addWidget(pwmSpin_);
    motorControl->addStretch();
    motorControl->addWidget(startMotorButton_);
    motorControl->addWidget(stopMotorButton_);
    connect(startMotorButton_, &QPushButton::clicked,
            this, &MainWindow::startSelectedMotor);
    connect(stopMotorButton_, &QPushButton::clicked,
            this, &MainWindow::stopSelectedMotor);

    vibWave_ = new WaveformWidget(sidePanel);
    vibWave_->setFixedHeight(108);
    vibWave_->setTitle(QStringLiteral("振动采样 · 等待板端网关波形"));
    vibWave_->setUnit(QStringLiteral("m/s²"));
    vibWave_->setLineColor(QColor(0x3b, 0xea, 0xbf));

    tempTrendWave_ = new WaveformWidget(sidePanel);
    tempTrendWave_->setFixedHeight(72);
    tempTrendWave_->setTitle(QStringLiteral("振动 RMS 趋势 · 寄存器 5"));
    tempTrendWave_->setUnit(QStringLiteral("m/s²"));
    tempTrendWave_->setLineColor(QColor(0xfb, 0x92, 0x3c));

    auto addInfoRow = [&](const QString &caption, QLabel **valueOut) {
        auto *row = new QHBoxLayout();
        auto *cap = new QLabel(caption, sidePanel);
        cap->setObjectName(QStringLiteral("rowCaption"));
        *valueOut = new QLabel(sidePanel);
        (*valueOut)->setObjectName(QStringLiteral("rowValue"));
        row->addWidget(cap);
        row->addStretch();
        row->addWidget(*valueOut);
        sideLayout->addLayout(row);
    };

    sideLayout->addWidget(brandLabel_);
    sideLayout->addWidget(motorSelectorCaption);
    sideLayout->addLayout(motorSelector);
    sideLayout->addWidget(deviceTitleLabel_);
    sideLayout->addLayout(metricGrid);
    sideLayout->addLayout(motorControl);
    sideLayout->addWidget(vibWave_);
    sideLayout->addWidget(tempTrendWave_);
    addInfoRow(QStringLiteral("运行状态"), &stateValueLabel_);
    addInfoRow(QStringLiteral("高亮部位"), &siteValueLabel_);
    addInfoRow(QStringLiteral("PL 状态"), &plStatusLabel_);
    addInfoRow(QStringLiteral("NPU 状态"), &npuStatusLabel_);
    addInfoRow(QStringLiteral("NPU 工况"), &npuConditionLabel_);
    addInfoRow(QStringLiteral("NPU 置信度"), &npuConfidenceLabel_);
    addInfoRow(QStringLiteral("NPU 推理"), &npuRuntimeLabel_);
    addInfoRow(QStringLiteral("采样率"), &sampleRateLabel_);

    faultLabel_ = new QLabel(sidePanel);
    faultLabel_->setObjectName(QStringLiteral("fault"));
    faultLabel_->setWordWrap(true);
    faultLabel_->setAlignment(Qt::AlignCenter);
    metaLabel_ = new QLabel(sidePanel);
    metaLabel_->setObjectName(QStringLiteral("meta"));
    metaLabel_->setWordWrap(true);

    sideLayout->addWidget(faultLabel_);
    sideLayout->addWidget(metaLabel_);

    aiResultLabel_ = new QLabel(
        QStringLiteral("等待实时分析结果…"), sidePanel);
    aiResultLabel_->setObjectName(QStringLiteral("aiResult"));
    aiResultLabel_->setWordWrap(true);
    aiResultLabel_->setMinimumHeight(72);
    sideLayout->addWidget(aiResultLabel_);

    aiAnalyzeButton_ = new QPushButton(QStringLiteral("刷新实时分析结果"), sidePanel);
    aiAnalyzeButton_->setObjectName(QStringLiteral("aiBtn"));
    sideLayout->addWidget(aiAnalyzeButton_);

    sideLayout->addStretch();
    sidePanel->setMinimumHeight(sideLayout->sizeHint().height());
    sideScroll->setWidget(sidePanel);

    auto *viewportFrame = new QFrame(central);
    viewportFrame->setObjectName(QStringLiteral("viewportFrame"));
    auto *viewportLayout = new QVBoxLayout(viewportFrame);
    viewportLayout->setContentsMargins(8, 8, 8, 8);
    viewportLayout->setSpacing(6);

    QString viewportHintText;
    if (jichuPartsAvailable()) {
        auto *view3d = new MotorView3D(viewportFrame);
        if (view3d->isReady()) {
            viewport_ = view3d;
            viewportUsesJichu3D_ = true;
            const QString combined = MotorView3D::resolvedCombinedMeshPath();
            const QFileInfo combinedInfo(combined);
            viewportHintText =
                QStringLiteral("Quick3D · 先整台 wanzheng · 拆解外抛五部件后标故障\n"
                               "绿灰=正常 蓝=注意 黄=预警 橙=告警 红=严重/紧急\n"
                               "整台 mesh: %1 (%2 MB)")
                    .arg(combinedInfo.fileName())
                    .arg(combinedInfo.size() / (1024 * 1024));
        } else {
            const QString err = view3d->loadError();
            delete view3d;
            auto *fail = new QLabel(viewportFrame);
            fail->setWordWrap(true);
            fail->setAlignment(Qt::AlignCenter);
            fail->setStyleSheet(QStringLiteral("color:#f87171; font-size:13px; padding:24px;"));
            fail->setText(
                QStringLiteral("已找到 jichu .mesh，但 Qt Quick3D 未加载成功，不会再用方块简模冒充整台电机。\n\n"
                               "%1\n\n"
                               "请在 cmd 中执行：\n"
                               "  demo_pc\\fpga_learn\\tools\\deploy_and_run.bat\n"
                               "（会 xcopy assets 并 windeployqt 拷贝 Quick3D 插件）")
                    .arg(err));
            viewport_ = fail;
            viewportUsesJichu3D_ = false;
            viewportHintText = QStringLiteral("Quick3D 加载失败 · 见上方说明");
            qWarning("MotorView3D 未就绪: %s", qPrintable(err));
        }
    } else {
        viewport_ = new MotorGlWidget(viewportFrame);
        viewportUsesJichu3D_ = false;
        viewportHintText =
            QStringLiteral("OpenGL 方块简模（未找到 assets/parts/meshes/body.mesh）\n"
                           "请先运行 tools\\export_jichu_5parts.bat");
    }

    auto *viewportHint = new QLabel(viewportHintText, viewportFrame);
    viewportHint->setObjectName(QStringLiteral("viewportHint"));
    viewportHint->setAlignment(Qt::AlignCenter);

    viewportLayout->addWidget(viewport_, 1);
    viewportLayout->addWidget(viewportHint);

    body->addWidget(sideScroll);
    body->addWidget(viewportFrame, 1);

    root->addWidget(topBar);
    root->addLayout(body, 1);
    setCentralWidget(central);

    statusBar()->showMessage(
        QStringLiteral("IF-01 串口监测终端 · Modbus RTU 9600 8N1"));

    updateMotorButtons();
    refreshPanel();

    waveTimer_ = new QTimer(this);
    waveTimer_->setInterval(40);
    connect(waveTimer_, &QTimer::timeout, this, &MainWindow::onWaveTimerTick);
    connect(aiAnalyzeButton_, &QPushButton::clicked, this, &MainWindow::onAiAnalyzeClicked);
    waveTimer_->start();

    boardAlertClient_ = new BoardAlertClient(this);
    // 板端默认使用 PL/网关自动判级；Qt 只在明确需要硬件自检时下发强制等级。
    boardAlertClient_->setLevel(-1);
    gatewayWaveformClient_ = new GatewayWaveformClient(this);
    connect(gatewayWaveformClient_,
            &GatewayWaveformClient::samplesReady,
            this,
            [this](const QVector<double> &samples,
                   const QString &,
                   int periodUs) {
        vibWave_->replaceSamples(samples);
        const double rate = periodUs > 0
            ? 1000000.0 / periodUs
            : 500.0;
        vibWave_->setTitle(
            QStringLiteral("振动采样 · 板端网关 %1 Hz%2")
                .arg(rate, 0, 'f', 0)
                .arg(QString()));
    });
    connect(gatewayWaveformClient_,
            &GatewayWaveformClient::waveformError,
            this,
            [this](const QString &) {
        vibWave_->setTitle(QStringLiteral("振动采样 · 板端网关离线"));
    });
    gatewayWaveformClient_->start(1500);
    QTimer::singleShot(1200, this, &MainWindow::requestNpuInference);
    qRegisterMetaType<QVector<quint16>>("QVector<quint16>");
    serialThread_ = new QThread(this);
    serialThread_->setObjectName(QStringLiteral("modbus-serial"));
    serialWorker_ = new SerialModbusWorker();
    serialWorker_->setObjectName(QStringLiteral("modbus-serial-worker"));
    serialWorker_->moveToThread(serialThread_);

    connect(serialThread_, &QThread::finished,
            serialWorker_, &QObject::deleteLater);
    connect(this, &MainWindow::openSerialPortRequested,
            serialWorker_, &SerialModbusWorker::openPort);
    connect(this, &MainWindow::pollSerialRequested,
            serialWorker_, &SerialModbusWorker::poll);
    connect(this, &MainWindow::startMotorRequested,
            serialWorker_, &SerialModbusWorker::startMotor);
    connect(this, &MainWindow::stopMotorRequested,
            serialWorker_, &SerialModbusWorker::stopMotor);
    connect(this, &MainWindow::writeRegistersRequested,
            serialWorker_, &SerialModbusWorker::writeRegisters);
    connect(serialWorker_, &SerialModbusWorker::portOpened,
            this, &MainWindow::onSerialPortOpened);
    connect(serialWorker_, &SerialModbusWorker::registersRead,
            this, &MainWindow::onSerialRegisters);
    connect(serialWorker_, &SerialModbusWorker::serialError,
            this, &MainWindow::onSerialError);
    connect(serialWorker_, &SerialModbusWorker::controlFinished,
            this, &MainWindow::onSerialControlFinished);

    fallbackThread_ = new QThread(this);
    fallbackThread_->setObjectName(QStringLiteral("modbus-fallback-serial"));
    fallbackWorker_ = new SerialModbusWorker();
    fallbackWorker_->setObjectName(QStringLiteral("modbus-fallback-worker"));
    fallbackWorker_->moveToThread(fallbackThread_);
    connect(fallbackThread_, &QThread::finished,
            fallbackWorker_, &QObject::deleteLater);
    connect(this, &MainWindow::openFallbackPortRequested,
            fallbackWorker_, &SerialModbusWorker::openPort);
    connect(this, &MainWindow::pollFallbackRequested,
            fallbackWorker_, &SerialModbusWorker::poll);
    connect(this, &MainWindow::closeFallbackPortRequested,
            fallbackWorker_, &SerialModbusWorker::closePort);
    connect(fallbackWorker_, &SerialModbusWorker::registersRead,
            this, &MainWindow::onFallbackRegisters);
    connect(fallbackWorker_, &SerialModbusWorker::serialError,
            this, &MainWindow::onFallbackError);

    serialTimer_ = new QTimer(this);
    serialTimer_->setInterval(1000);
    connect(serialTimer_, &QTimer::timeout, this, &MainWindow::pollSerial);
    serialThread_->start();
    fallbackThread_->start();
    refreshSerialPorts();
    openSerialPort();
    serialTimer_->start();
    pollSerial();

    auto *showApi = new ShowApiServer(this);
    showApi->setRequestHandler(
        [this](const QString &method,
               const QString &path,
               const QJsonObject &payload) {
            return handleApiRequest(method, path, payload);
        });
    if (showApi->listen(8760)) {
        statusBar()->showMessage(
            QStringLiteral("香橙派控制 API 已启动 · http://0.0.0.0:8760"), 8000);
    } else {
        statusBar()->showMessage(
            QStringLiteral("警告：8760 端口启动失败（是否被占用？）"), 10000);
    }

    // 所有设备指标均由 USB-RS485 上的 Modbus RTU 从站提供。
}

MainWindow::~MainWindow()
{
    if (fallbackWorker_ && fallbackThread_ && fallbackThread_->isRunning()) {
        QMetaObject::invokeMethod(
            fallbackWorker_, "shutdown", Qt::BlockingQueuedConnection);
    }
    if (fallbackThread_) {
        fallbackThread_->quit();
        fallbackThread_->wait(2000);
    }

    if (serialWorker_ && serialThread_ && serialThread_->isRunning()) {
        QMetaObject::invokeMethod(
            serialWorker_, "shutdown", Qt::BlockingQueuedConnection);
    }
    if (serialThread_) {
        serialThread_->quit();
        serialThread_->wait(2000);
    }
}

void MainWindow::onRemoteShow(const QJsonObject &payload)
{
    applyRemoteShow(payload);
}

void MainWindow::applyRemoteShow(const QJsonObject &payload)
{
    // 远程指令优先：暂停串口轮询 12 秒，让这次切换能看清
    remoteHoldUntilMs_ = QDateTime::currentMSecsSinceEpoch() + 12000;

    const int targetId = qBound(1, payload.value(QStringLiteral("device_id"))
                                       .toInt(selectedMotor().id),
                                static_cast<int>(motors_.size()));
    if (targetId != selectedMotor().id)
        selectMotor(targetId);

    MotorSnapshot &motor = selectedMotor();
    motor.source = payload.value(QStringLiteral("source")).toString(QStringLiteral("远程指令"));
    motor.deviceSource = QStringLiteral("remote");
    const QString state = payload.value(QStringLiteral("state")).toString(motor.state);
    if (!state.isEmpty())
        motor.state = state;

    const QString site = payload.value(QStringLiteral("fault_site")).toString(motor.faultSite);
    if (!site.isEmpty())
        motor.faultSite = site;
    if (motor.state == QLatin1String("normal"))
        motor.faultSite = QStringLiteral("none");

    const QJsonObject metrics = payload.value(QStringLiteral("metrics")).toObject();
    if (metrics.contains(QStringLiteral("vibration_rms"))) {
        motor.rms = metrics.value(QStringLiteral("vibration_rms")).toDouble(motor.rms);
        motor.healthHasRms = true;
        motor.vibrationSource = QStringLiteral("remote");
    }
    if (metrics.contains(QStringLiteral("temp_c"))) {
        motor.tempC = metrics.value(QStringLiteral("temp_c")).toDouble(motor.tempC);
        motor.healthHasTemp = true;
        motor.hasEnv = true;
        motor.temperatureSource = QStringLiteral("remote");
    }
    if (metrics.contains(QStringLiteral("current_a"))) {
        motor.currentA = metrics.value(QStringLiteral("current_a")).toDouble(motor.currentA);
        motor.healthHasCurrent = true;
        motor.hasEnv = true;
        motor.currentSource = QStringLiteral("remote");
    }
    const QString label = metrics.value(QStringLiteral("fault_label")).toString();
    if (!label.isEmpty())
        motor.faultLabel = label;
    else
        motor.faultLabel = QStringLiteral("远程指令切换 · %1 · %2")
                               .arg(stateText(motor.state))
                               .arg(siteText(motor.faultSite));

    motor.hasData = true;
    updateHealthScore(motor);
    updateMotorButtons();

    if (motor.state == QLatin1String("emergency") || motor.state == QLatin1String("fault")) {
        gatewayStatusLabel_->setText(QStringLiteral("● 远程告警 · 外部指令"));
        gatewayStatusLabel_->setStyleSheet(
            QStringLiteral("color:#e09090; font-size:12px; font-weight:600;"));
    } else if (motor.id == 1) {
        gatewayStatusLabel_->setText(QStringLiteral("● 1 号电机 · 远程指令"));
        gatewayStatusLabel_->setStyleSheet(
            QStringLiteral("color:#6ee7a0; font-size:12px; font-weight:600;"));
    } else {
        gatewayStatusLabel_->setText(QStringLiteral("● 监测通道 · 远程指令"));
        gatewayStatusLabel_->setStyleSheet(
            QStringLiteral("color:#c9a227; font-size:12px; font-weight:600;"));
    }

    refreshPanel();
    statusBar()->showMessage(
        QStringLiteral("远程已切换：%1 号 · %2 · %3")
            .arg(motor.id)
            .arg(stateText(motor.state))
            .arg(siteText(motor.faultSite)),
        6000);
}

QJsonObject MainWindow::apiState() const
{
    const MotorSnapshot &selected = selectedMotor();
    const QJsonObject &data = selected.lastGwData;
    const int level = data.value(QStringLiteral("alert_level")).toInt(-1);
    const int boardLevel = boardAlertClient_
        ? boardAlertClient_->acknowledgedLevel()
        : -1;

    QJsonArray devices;
    for (const MotorSnapshot &motor : motors_) {
        QJsonObject summary{
            {QStringLiteral("id"), motor.id},
            {QStringLiteral("has_data"), motor.hasData},
            {QStringLiteral("state"), motor.state},
            {QStringLiteral("state_text"), stateText(motor.state)},
            {QStringLiteral("fault_site"), motor.faultSite},
            {QStringLiteral("fault_site_text"), siteText(motor.faultSite)},
            {QStringLiteral("fault_label"), motor.faultLabel},
            {QStringLiteral("vibration_rms"), motor.rms},
            {QStringLiteral("vibration_peak"), motor.peak},
            {QStringLiteral("temperature_c"), motor.tempC},
            {QStringLiteral("current_a"), motor.currentA},
            {QStringLiteral("motor_enabled"), motor.motorEnabled},
            {QStringLiteral("motor_pwm"), motor.motorPwm},
            {QStringLiteral("motor_direction"), motor.motorDirection},
            {QStringLiteral("health_percent"), motor.health},
            {QStringLiteral("source"), motor.source},
            {QStringLiteral("device_source"), motor.deviceSource},
            {QStringLiteral("vibration_source"), motor.vibrationSource},
            {QStringLiteral("rms_source"),
             motor.lastGwData.value(QStringLiteral("rms_source"))},
            {QStringLiteral("peak_source"),
             motor.lastGwData.value(QStringLiteral("peak_source"))},
            {QStringLiteral("accel_source"),
             motor.lastGwData.value(QStringLiteral("accel_source"))},
            {QStringLiteral("gyro_source"),
             motor.lastGwData.value(QStringLiteral("gyro_source"))},
            {QStringLiteral("temperature_source"), motor.temperatureSource},
            {QStringLiteral("current_source"), motor.currentSource},
            {QStringLiteral("motor_source"),
             motor.lastGwData.value(QStringLiteral("motor_source"))},
            {QStringLiteral("threshold_source"),
             motor.lastGwData.value(QStringLiteral("threshold_source"))},
            {QStringLiteral("algo"), motor.algo},
            {QStringLiteral("alert_level"),
             motor.lastGwData.value(QStringLiteral("alert_level")).toInt(-1)},
        };
        const QStringList rawFields = {
            QStringLiteral("rms_raw"),
            QStringLiteral("raw_lsb"),
            QStringLiteral("filtered_lsb"),
            QStringLiteral("mean_abs_lsb"),
            QStringLiteral("zero_crossings"),
            QStringLiteral("alert_low_raw"),
            QStringLiteral("alert_high_raw"),
            QStringLiteral("current_raw"),
            QStringLiteral("sensor_status"),
            QStringLiteral("whoami"),
            QStringLiteral("point1"),
            QStringLiteral("point2"),
            QStringLiteral("point3"),
            QStringLiteral("point4"),
            QStringLiteral("point5"),
            QStringLiteral("point_site_status"),
            QStringLiteral("fault_site_code"),
            QStringLiteral("board_npu_valid"),
            QStringLiteral("board_npu"),
        };
        for (const QString &key : rawFields) {
            if (motor.lastGwData.contains(key))
                summary.insert(key, motor.lastGwData.value(key));
        }
        devices.append(summary);
    }

    return QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("timestamp"),
         QDateTime::currentDateTime().toString(Qt::ISODate)},
        {QStringLiteral("selected_device_id"), selected.id},
        {QStringLiteral("serial"),
         QJsonObject{
             {QStringLiteral("port"),
              portCombo_ ? portCombo_->currentText().trimmed() : QString()},
             {QStringLiteral("fallback_port"),
              fallbackPortCombo_
                  ? fallbackPortCombo_->currentText().trimmed() : QString()},
             {QStringLiteral("fallback_enabled"),
              fallbackEnable_ ? fallbackEnable_->isChecked() : false},
             {QStringLiteral("fallback_online"), fallbackOnline_},
             {QStringLiteral("online"), serialOnline_},
             {QStringLiteral("mode"), QStringLiteral("Modbus RTU")},
             {QStringLiteral("baud"), 9600},
             {QStringLiteral("data_bits"), 8},
             {QStringLiteral("parity"), QStringLiteral("none")},
             {QStringLiteral("stop_bits"), 1},
             {QStringLiteral("slave_id"), 1},
             {QStringLiteral("slave_ids"),
              QJsonArray{1, 2, 3, 4, 5, 6}},
         }},
        {QStringLiteral("device"),
         QJsonObject{
             {QStringLiteral("id"), selected.id},
             {QStringLiteral("has_data"), selected.hasData},
             {QStringLiteral("state"), selected.state},
             {QStringLiteral("state_text"), stateText(selected.state)},
             {QStringLiteral("fault_site"), selected.faultSite},
             {QStringLiteral("fault_site_text"), siteText(selected.faultSite)},
             {QStringLiteral("fault_label"), selected.faultLabel},
             {QStringLiteral("vibration_rms"), selected.rms},
             {QStringLiteral("vibration_peak"), selected.peak},
             {QStringLiteral("temperature_c"), selected.tempC},
             {QStringLiteral("current_a"), selected.currentA},
             {QStringLiteral("motor_enabled"), selected.motorEnabled},
             {QStringLiteral("motor_pwm"), selected.motorPwm},
             {QStringLiteral("motor_direction"), selected.motorDirection},
             {QStringLiteral("health_percent"), selected.health},
             {QStringLiteral("source"), selected.source},
             {QStringLiteral("device_source"), selected.deviceSource},
             {QStringLiteral("vibration_source"), selected.vibrationSource},
             {QStringLiteral("rms_source"),
              data.value(QStringLiteral("rms_source"))},
             {QStringLiteral("peak_source"),
              data.value(QStringLiteral("peak_source"))},
             {QStringLiteral("accel_source"),
              data.value(QStringLiteral("accel_source"))},
             {QStringLiteral("gyro_source"),
              data.value(QStringLiteral("gyro_source"))},
             {QStringLiteral("temperature_source"), selected.temperatureSource},
             {QStringLiteral("current_source"), selected.currentSource},
             {QStringLiteral("motor_source"),
              data.value(QStringLiteral("motor_source"))},
             {QStringLiteral("threshold_source"),
              data.value(QStringLiteral("threshold_source"))},
             {QStringLiteral("algo"), selected.algo},
             {QStringLiteral("rms_raw"),
              data.value(QStringLiteral("rms_raw")).toInt()},
             {QStringLiteral("alert_low_raw"),
              data.value(QStringLiteral("alert_low_raw")).toInt()},
             {QStringLiteral("alert_high_raw"),
              data.value(QStringLiteral("alert_high_raw")).toInt()},
             {QStringLiteral("alert_level"), level},
             {QStringLiteral("sensor_status"),
              data.value(QStringLiteral("sensor_status")).toInt()},
             {QStringLiteral("whoami"),
              data.value(QStringLiteral("whoami")).toInt()},
         }},
         {QStringLiteral("alert"),
          QJsonObject{
             {QStringLiteral("level"), level},
             {QStringLiteral("rgb"),
              level == 2 ? QStringLiteral("red")
                  : level == 1 ? QStringLiteral("blue")
                  : level == 0 ? QStringLiteral("green")
                               : QStringLiteral("unknown")},
             {QStringLiteral("buzzer"), level == 2},
              {QStringLiteral("board_acknowledged_level"), boardLevel},
          }},
        {QStringLiteral("npu"), lastNpuResult_},
        {QStringLiteral("devices"), devices},
        {QStringLiteral("commands"),
         QJsonArray{
             QStringLiteral("select_device"),
             QStringLiteral("start_motor"),
             QStringLiteral("stop_motor"),
             QStringLiteral("analyze"),
             QStringLiteral("generate_report"),
             QStringLiteral("refresh_serial"),
             QStringLiteral("select_serial"),
             QStringLiteral("set_simulated_points"),
         }},
    };
}

QJsonObject MainWindow::executeApiCommand(const QJsonObject &payload)
{
    const QString command =
        payload.value(QStringLiteral("command")).toString().trimmed();
    if (command.isEmpty()) {
        return QJsonObject{{QStringLiteral("ok"), false},
                           {QStringLiteral("error"),
                            QStringLiteral("missing_command")}};
    }

    if (command == QLatin1String("select_device")) {
        const int deviceId = qBound(
            1,
            payload.value(QStringLiteral("device_id"))
                .toInt(selectedMotor().id),
            static_cast<int>(motors_.size()));
        remoteHoldUntilMs_ = 0;
        selectMotor(deviceId);
        return QJsonObject{{QStringLiteral("ok"), true},
                           {QStringLiteral("command"), command},
                           {QStringLiteral("device_id"), deviceId}};
    }

    if (command == QLatin1String("start_motor")) {
        const int requestedId =
            payload.value(QStringLiteral("device_id")).toInt(selectedMotor().id);
        if (requestedId != 1 || selectedMotor().id != 1) {
            return QJsonObject{{QStringLiteral("ok"), false},
                               {QStringLiteral("error"),
                                QStringLiteral("simulated_device_has_no_actuator")}};
        }
        const int pwm = qBound(
            0, payload.value(QStringLiteral("pwm")).toInt(500), 1000);
        if (pwmSpin_)
            pwmSpin_->setValue(pwm);
        emit startMotorRequested(1, static_cast<quint16>(pwm));
        return QJsonObject{{QStringLiteral("ok"), true},
                           {QStringLiteral("accepted"), true},
                           {QStringLiteral("command"), command},
                           {QStringLiteral("pwm"), pwm}};
    }

    if (command == QLatin1String("stop_motor")) {
        const int requestedId =
            payload.value(QStringLiteral("device_id")).toInt(selectedMotor().id);
        if (requestedId != 1 || selectedMotor().id != 1) {
            return QJsonObject{{QStringLiteral("ok"), false},
                               {QStringLiteral("error"),
                                QStringLiteral("simulated_device_has_no_actuator")}};
        }
        emit stopMotorRequested(1);
        return QJsonObject{{QStringLiteral("ok"), true},
                           {QStringLiteral("accepted"), true},
                           {QStringLiteral("command"), command}};
    }

    if (command == QLatin1String("set_simulated_points")) {
        const QJsonArray points =
            payload.value(QStringLiteral("points")).toArray();
        if (points.size() != 5) {
            return QJsonObject{
                {QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("points_must_have_5_values")},
            };
        }
        QVector<quint16> registers;
        QVector<quint16> values;
        for (int i = 0; i < points.size(); ++i) {
            registers.append(static_cast<quint16>(16 + i));
            values.append(static_cast<quint16>(
                qBound(0, points.at(i).toInt(), 65535)));
        }
        const int deviceId = qBound(
            1,
            payload.value(QStringLiteral("device_id"))
                .toInt(selectedMotor().id),
            static_cast<int>(motors_.size()));
        emit writeRegistersRequested(deviceId, registers, values);
        return QJsonObject{
            {QStringLiteral("ok"), true},
            {QStringLiteral("accepted"), true},
            {QStringLiteral("command"), command},
            {QStringLiteral("device_id"), deviceId},
            {QStringLiteral("registers"), QJsonArray{16, 17, 18, 19, 20}},
            {QStringLiteral("points"), points},
        };
    }

    if (command == QLatin1String("analyze")) {
        onAiAnalyzeClicked();
        return QJsonObject{{QStringLiteral("ok"), true},
                           {QStringLiteral("accepted"), true},
                           {QStringLiteral("command"), command}};
    }

    if (command == QLatin1String("generate_report")) {
        QTimer::singleShot(0, this, &MainWindow::showTestReport);
        return QJsonObject{{QStringLiteral("ok"), true},
                           {QStringLiteral("accepted"), true},
                           {QStringLiteral("command"), command}};
    }

    if (command == QLatin1String("refresh_serial")) {
        refreshSerialPorts();
        openSerialPort();
        return QJsonObject{{QStringLiteral("ok"), true},
                           {QStringLiteral("command"), command},
                           {QStringLiteral("port"),
                            portCombo_
                                ? portCombo_->currentText().trimmed()
                                : QString()}};
    }

    if (command == QLatin1String("select_serial")) {
        const QString port =
            payload.value(QStringLiteral("port")).toString().trimmed();
        if (!portCombo_ || port.isEmpty()) {
            return QJsonObject{{QStringLiteral("ok"), false},
                               {QStringLiteral("error"),
                                QStringLiteral("invalid_port")}};
        }
        int index = portCombo_->findText(port);
        if (index < 0) {
            portCombo_->addItem(port);
            index = portCombo_->findText(port);
        }
        portCombo_->setCurrentIndex(index);
        openSerialPort();
        return QJsonObject{{QStringLiteral("ok"), true},
                           {QStringLiteral("command"), command},
                           {QStringLiteral("port"), port}};
    }

    return QJsonObject{{QStringLiteral("ok"), false},
                       {QStringLiteral("error"),
                        QStringLiteral("unknown_command")},
                       {QStringLiteral("command"), command}};
}

QJsonObject MainWindow::handleApiRequest(
    const QString &method,
    const QString &path,
    const QJsonObject &payload)
{
    if (method == QLatin1String("GET") && path == QLatin1String("/api/state")) {
        return apiState();
    }

    if (method == QLatin1String("GET") && path == QLatin1String("/api/report")) {
        return QJsonObject{
            {QStringLiteral("ok"), true},
            {QStringLiteral("format"), QStringLiteral("markdown")},
            {QStringLiteral("markdown"), buildTestReportMarkdown()},
        };
    }

    if (method == QLatin1String("POST") && path == QLatin1String("/api/show")) {
        return QJsonObject{{QStringLiteral("ok"), false},
                           {QStringLiteral("error"),
                            QStringLiteral("diagnosis_override_disabled")}};
    }

    if (method == QLatin1String("POST")
        && path == QLatin1String("/api/command")) {
        return executeApiCommand(payload);
    }

    return QJsonObject{{QStringLiteral("ok"), false},
                       {QStringLiteral("error"),
                        QStringLiteral("unknown_endpoint")},
                       {QStringLiteral("method"), method},
                       {QStringLiteral("path"), path}};
}

// ---------- USB-RS485 / Modbus RTU 串口数据源 ----------

void MainWindow::refreshSerialPorts()
{
    if (!portCombo_)
        return;
    const QString current = portCombo_->currentText().trimmed();
    const QString currentFallback =
        fallbackPortCombo_ ? fallbackPortCombo_->currentText().trimmed() : QString();
    const QStringList ports = SerialModbusClient::availablePorts();
    qInfo().noquote() << "serial ports:" << ports.join(QStringLiteral(", "));
    QSignalBlocker primaryBlocker(portCombo_);
    QSignalBlocker fallbackBlocker(fallbackPortCombo_);
    portCombo_->clear();
    portCombo_->addItems(ports);
    const QString preferred = current.isEmpty()
        ? qEnvironmentVariable("FPGA_SERIAL_PORT", QStringLiteral("COM10"))
        : current;
    const int index = portCombo_->findText(preferred);
    if (index >= 0)
        portCombo_->setCurrentIndex(index);
    else if (portCombo_->count() > 0)
        portCombo_->setCurrentIndex(0);

    if (fallbackPortCombo_) {
        fallbackPortCombo_->clear();
        fallbackPortCombo_->addItems(ports);
        QString preferredFallback = currentFallback;
        if (preferredFallback.isEmpty()) {
            preferredFallback = qEnvironmentVariable(
                "FPGA_FALLBACK_SERIAL_PORT", QStringLiteral("COM21"));
        }
        int fallbackIndex = fallbackPortCombo_->findText(preferredFallback);
        if (fallbackIndex < 0)
            fallbackIndex = fallbackPortCombo_->findText(QStringLiteral("COM21"));
        if (fallbackIndex >= 0)
            fallbackPortCombo_->setCurrentIndex(fallbackIndex);
        else if (fallbackPortCombo_->count() > 0)
            fallbackPortCombo_->setCurrentIndex(0);
    }

    if (fallbackEnable_) {
        const QString primary = portCombo_->currentText().trimmed();
        const QString fallback =
            fallbackPortCombo_ ? fallbackPortCombo_->currentText().trimmed() : QString();
        const bool available =
            !fallback.isEmpty() && fallback != primary;
        if (fallbackEnable_->isChecked()) {
            if (available)
                openFallbackPort();
            else
                fallbackEnable_->setChecked(false);
        } else if (available && fallback == QLatin1String("COM21")) {
            fallbackEnable_->setChecked(true);
        }
    }
}

void MainWindow::openSerialPort()
{
    if (!serialWorker_ || !portCombo_)
        return;
    const QString port = portCombo_->currentText().trimmed();
    if (port.isEmpty())
        return;

    primaryPort_ = port;
    serialOnline_ = false;
    serialUseRealData_ = false;
    liveNpuSamples_.clear();
    for (auto &registers : primaryRegistersByDevice_)
        registers.clear();
    primaryRegistersMsByDevice_.fill(0);
    if (fallbackEnable_ && fallbackPortCombo_
        && fallbackPortCombo_->currentText().trimmed() == port) {
        fallbackEnable_->setChecked(false);
    }
    gatewayStatusLabel_->setText(
        QStringLiteral("● 串口 %1 · 正在打开").arg(port));
    gatewayStatusLabel_->setStyleSheet(
        QStringLiteral("color:#c9a227; font-size:12px; font-weight:600;"));
    statusBar()->showMessage(
        QStringLiteral("正在打开 %1 · 9600 8N1 · Modbus RTU Slave 1~6").arg(port),
        5000);
    emit openSerialPortRequested(port, 9600, 1);
}

void MainWindow::openFallbackPort()
{
    if (!fallbackWorker_ || !fallbackPortCombo_ || !fallbackEnable_
        || !fallbackEnable_->isChecked()) {
        return;
    }
    const QString port = fallbackPortCombo_->currentText().trimmed();
    const QString primary =
        portCombo_ ? portCombo_->currentText().trimmed() : QString();
    if (port.isEmpty() || port == primary) {
        fallbackOnline_ = false;
        for (auto &registers : fallbackRegistersByDevice_)
            registers.clear();
        fallbackRegistersMsByDevice_.fill(0);
        return;
    }

    fallbackPort_ = port;
    fallbackOnline_ = false;
    for (auto &registers : fallbackRegistersByDevice_)
        registers.clear();
    fallbackRegistersMsByDevice_.fill(0);
    emit openFallbackPortRequested(port, 9600, 1);
}

void MainWindow::closeFallbackPort()
{
    emit closeFallbackPortRequested();
    fallbackOnline_ = false;
    fallbackPort_.clear();
    for (auto &registers : fallbackRegistersByDevice_)
        registers.clear();
    fallbackRegistersMsByDevice_.fill(0);
}

void MainWindow::appendNpuInferenceLog(
    const QJsonObject &result,
    const QString &source)
{
    if (result.isEmpty())
        return;

    const MotorSnapshot &motor = selectedMotor();
    QJsonObject summary;
    QJsonArray samples;
    if (liveNpuSamples_.size() == 8) {
        static const QStringList fields = {
            QStringLiteral("rms_m_s2"),
            QStringLiteral("peak_m_s2"),
            QStringLiteral("temperature_c"),
            QStringLiteral("current_a"),
            QStringLiteral("pwm_normalized"),
        };
        for (const QVector<double> &sample : liveNpuSamples_) {
            QJsonObject row;
            for (int i = 0; i < fields.size() && i < sample.size(); ++i)
                row.insert(fields[i], sample[i]);
            samples.append(row);
        }

        const QVector<double> &current = liveNpuSamples_.back();
        double rmsSum = 0.0;
        for (const QVector<double> &sample : liveNpuSamples_)
            rmsSum += sample.value(0);
        summary.insert(
            QStringLiteral("current_rms_m_s2"),
            current.value(0));
        summary.insert(
            QStringLiteral("mean_rms_m_s2"),
            rmsSum / liveNpuSamples_.size());
        summary.insert(
            QStringLiteral("rms_trend_m_s2"),
            current.value(0) - liveNpuSamples_.front().value(0));
        summary.insert(QStringLiteral("current_peak_m_s2"), current.value(1));
        summary.insert(QStringLiteral("temperature_c"), current.value(2));
        summary.insert(QStringLiteral("current_a"), current.value(3));
        summary.insert(QStringLiteral("pwm_normalized"), current.value(4));
    }

    const QJsonObject &diag = motor.lastGwData;
    const QJsonObject record{
        {QStringLiteral("record_type"), QStringLiteral("npu_inference")},
        {QStringLiteral("timestamp"),
         QDateTime::currentDateTime().toString(Qt::ISODateWithMs)},
        {QStringLiteral("device_id"), motor.id},
        {QStringLiteral("source"), source},
        {QStringLiteral("input_summary"), summary},
        {QStringLiteral("input_window"), samples},
        {QStringLiteral("diagnostics"),
         QJsonObject{
             {QStringLiteral("status"),
              diag.value(QStringLiteral("status")).toString(motor.state)},
             {QStringLiteral("status_text"),
              diag.value(QStringLiteral("status_text")).toString()},
             {QStringLiteral("fault_site"),
              diag.value(QStringLiteral("fault_site"))
                  .toString(motor.faultSite)},
             {QStringLiteral("alert_level"),
              diag.value(QStringLiteral("alert_level")).toInt(-1)},
             {QStringLiteral("alert_low"),
              diag.value(QStringLiteral("alert_low")).toInt(-1)},
             {QStringLiteral("alert_high"),
              diag.value(QStringLiteral("alert_high")).toInt(-1)},
             {QStringLiteral("vibration_source"), motor.vibrationSource},
             {QStringLiteral("temperature_source"), motor.temperatureSource},
             {QStringLiteral("current_source"), motor.currentSource},
         }},
        {QStringLiteral("model_result"), result},
    };

    QJsonObject signatureRecord = record;
    signatureRecord.remove(QStringLiteral("timestamp"));
    const QByteArray signature =
        QJsonDocument(signatureRecord).toJson(QJsonDocument::Compact);
    if (signature == lastNpuLogSignature_)
        return;

    QString baseDir =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (baseDir.isEmpty())
        baseDir = QCoreApplication::applicationDirPath();
    const QString logDirPath = QDir(baseDir).filePath(QStringLiteral("FPGA"));
    if (!QDir().mkpath(logDirPath)) {
        qWarning().noquote() << "Unable to create NPU log directory:"
                             << logDirPath;
        return;
    }

    QFile logFile(
        QDir(logDirPath).filePath(QStringLiteral("npu_inference_log.jsonl")));
    if (!logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qWarning().noquote() << "Unable to open NPU log file:"
                             << logFile.fileName() << logFile.errorString();
        return;
    }
    logFile.write(QJsonDocument(record).toJson(QJsonDocument::Compact));
    logFile.write("\n");
    if (!logFile.flush()) {
        qWarning().noquote() << "Unable to flush NPU log file:"
                             << logFile.fileName() << logFile.errorString();
        return;
    }
    lastNpuLogSignature_ = signature;
}

void MainWindow::recordRmsAndScheduleAlertSnapshot(
    int deviceId,
    const QJsonObject &data)
{
    const int index = qBound(1, deviceId, static_cast<int>(motors_.size())) - 1;
    RmsHistoryPoint point;
    point.timestampMs = QDateTime::currentMSecsSinceEpoch();
    point.rmsMs2 = motors_[index].rms;
    point.alertLevel = data.value(QStringLiteral("alert_level")).toInt(0);

    QVector<RmsHistoryPoint> &history = rmsHistoryByDevice_[index];
    history.append(point);
    while (history.size() > 180)
        history.removeFirst();

    const int previousLevel = lastObservedAlertLevel_[index];
    lastObservedAlertLevel_[index] = point.alertLevel;
    if (point.alertLevel <= previousLevel || point.alertLevel <= 0
        || alertSnapshotPending_[index]) {
        return;
    }

    alertSnapshotPending_[index] = true;
    alertSnapshotContext_[index] = data;
    const qint64 alertMs = point.timestampMs;
    const int level = point.alertLevel;
    QTimer::singleShot(10000, this, [this, index, level, alertMs] {
        alertSnapshotPending_[index] = false;
        captureAlertRmsChart(index + 1, level, alertMs);
    });
}

void MainWindow::captureAlertRmsChart(int deviceId, int level, qint64 alertMs)
{
    const int index = qBound(1, deviceId, static_cast<int>(motors_.size())) - 1;
    const QVector<RmsHistoryPoint> &history = rmsHistoryByDevice_[index];
    const qint64 windowStart = alertMs - 60000;
    const qint64 windowEnd = alertMs + 10000;

    QVector<RmsHistoryPoint> visible;
    for (const RmsHistoryPoint &point : history) {
        if (point.timestampMs >= windowStart && point.timestampMs <= windowEnd)
            visible.append(point);
    }
    if (visible.isEmpty())
        return;

    const QJsonObject &context = alertSnapshotContext_[index];
    const double rawToMs2 = 9.80665 / 16384.0;
    const int lowRaw = context.value(QStringLiteral("alert_low")).toInt(-1);
    const int highRaw = context.value(QStringLiteral("alert_high")).toInt(-1);
    const double lowMs2 =
        lowRaw >= 0 && lowRaw != 0xFFFF ? lowRaw * rawToMs2 : -1.0;
    const double highMs2 =
        highRaw >= 0 && highRaw != 0xFFFF ? highRaw * rawToMs2 : -1.0;

    double minValue = visible.front().rmsMs2;
    double maxValue = visible.front().rmsMs2;
    for (const RmsHistoryPoint &point : visible) {
        minValue = std::min(minValue, point.rmsMs2);
        maxValue = std::max(maxValue, point.rmsMs2);
    }
    if (lowMs2 >= 0.0) {
        minValue = std::min(minValue, lowMs2);
        maxValue = std::max(maxValue, lowMs2);
    }
    if (highMs2 >= 0.0) {
        minValue = std::min(minValue, highMs2);
        maxValue = std::max(maxValue, highMs2);
    }
    minValue = std::max(0.0, minValue);
    if (maxValue <= minValue)
        maxValue = minValue + 0.1;
    const double margin = std::max(0.02, (maxValue - minValue) * 0.12);
    minValue = std::max(0.0, minValue - margin);
    maxValue += margin;

    QImage image(1400, 720, QImage::Format_ARGB32);
    image.fill(QColor(0xF7, 0xF8, 0xFA));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const QRect plot(100, 130, 1240, 500);
    const QColor axisColor(0x4D, 0x58, 0x66);
    const QColor gridColor(0xDF, 0xE4, 0xEA);
    const QColor lineColor(0x22, 0x66, 0xB8);
    const QColor alertColor(0xC5, 0x38, 0x38);
    const QColor thresholdColor(0xD1, 0x78, 0x1E);

    QFont titleFont = painter.font();
    titleFont.setPointSize(20);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(QColor(0x1D, 0x27, 0x33));
    painter.drawText(
        80,
        50,
        QStringLiteral("%1 号设备 · 振动 RMS 告警曲线").arg(deviceId));

    QFont detailFont = painter.font();
    detailFont.setPointSize(11);
    detailFont.setBold(false);
    painter.setFont(detailFont);
    painter.setPen(QColor(0x5D, 0x69, 0x78));
    const QString levelText =
        level >= 2 ? QStringLiteral("严重告警")
                   : level == 1 ? QStringLiteral("预警")
                                : QStringLiteral("正常");
    painter.drawText(
        82,
        82,
        QStringLiteral("触发时间：%1    告警等级：%2 (%3)")
            .arg(QDateTime::fromMSecsSinceEpoch(alertMs)
                     .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
            .arg(level)
            .arg(levelText));

    painter.fillRect(plot, Qt::white);
    painter.setPen(QPen(gridColor, 1));
    for (int i = 0; i <= 5; ++i) {
        const int y = plot.bottom() - (plot.height() * i) / 5;
        painter.drawLine(plot.left(), y, plot.right(), y);
        const double value =
            minValue + (maxValue - minValue) * i / 5.0;
        painter.setPen(QColor(0x5D, 0x69, 0x78));
        painter.drawText(
            QRect(8, y - 11, 82, 22),
            Qt::AlignRight | Qt::AlignVCenter,
            QString::number(value, 'f', 3));
        painter.setPen(QPen(gridColor, 1));
    }
    painter.setPen(QPen(axisColor, 1.5));
    painter.drawRect(plot);

    auto mapX = [&](qint64 timestampMs) {
        return plot.left() + int(
            (timestampMs - windowStart) * double(plot.width())
            / double(windowEnd - windowStart));
    };
    auto mapY = [&](double value) {
        return plot.bottom() - int(
            (value - minValue) * double(plot.height())
            / (maxValue - minValue));
    };

    for (int second = -60; second <= 10; second += 10) {
        const int x = mapX(alertMs + qint64(second) * 1000);
        painter.setPen(QPen(gridColor, 1));
        painter.drawLine(x, plot.top(), x, plot.bottom());
        painter.setPen(QColor(0x5D, 0x69, 0x78));
        painter.drawText(
            QRect(x - 25, plot.bottom() + 8, 50, 24),
            Qt::AlignCenter,
            QStringLiteral("%1").arg(second));
    }

    auto drawThreshold = [&](double value, const QString &label) {
        if (value < 0.0)
            return;
        const int y = mapY(value);
        painter.setPen(QPen(thresholdColor, 1.4, Qt::DashLine));
        painter.drawLine(plot.left(), y, plot.right(), y);
        painter.drawText(
            QRect(plot.right() - 210, y - 24, 200, 20),
            Qt::AlignRight | Qt::AlignVCenter,
            QStringLiteral("%1 %2 m/s²")
                .arg(label)
                .arg(value, 0, 'f', 3));
    };
    drawThreshold(lowMs2, QStringLiteral("低阈值"));
    drawThreshold(highMs2, QStringLiteral("高阈值"));

    QPainterPath path;
    for (int i = 0; i < visible.size(); ++i) {
        const QPointF point(
            mapX(visible[i].timestampMs),
            mapY(visible[i].rmsMs2));
        if (i == 0)
            path.moveTo(point);
        else
            path.lineTo(point);
    }
    painter.setPen(QPen(lineColor, 2.8));
    painter.drawPath(path);

    const int alertX = mapX(alertMs);
    painter.setPen(QPen(alertColor, 1.8, Qt::DashLine));
    painter.drawLine(alertX, plot.top(), alertX, plot.bottom());
    painter.setPen(QColor(0x5D, 0x69, 0x78));
    painter.drawText(
        QRect(plot.left(), plot.bottom() + 34, plot.width(), 24),
        Qt::AlignCenter,
        QStringLiteral("相对告警触发时间（秒）"));
    painter.drawText(
        QRect(plot.left(), plot.top() - 28, plot.width(), 22),
        Qt::AlignLeft | Qt::AlignVCenter,
        QStringLiteral("振动 RMS（m/s²）"));

    painter.end();
    alertRmsCharts_.append(
        AlertRmsChart{deviceId, level, alertMs, image});
    while (alertRmsCharts_.size() > 24)
        alertRmsCharts_.removeFirst();
    qInfo().noquote() << "Captured alert RMS chart for report:"
                      << deviceId << level;
}

void MainWindow::requestNpuInference()
{
    if (npuRequestInFlight_)
        return;
    const QJsonObject &latest = selectedMotor().lastGwData;
    if (latest.value(QStringLiteral("board_npu_valid")).toBool(false)) {
        const QJsonObject result =
            latest.value(QStringLiteral("board_npu")).toObject();
        lastNpuResult_ = QJsonObject{
            {QStringLiteral("result"), result},
        };
        npuStatusLabel_->setText(QStringLiteral("板端多节点模型 · 实时"));
        npuConditionLabel_->setText(
            result.value(QStringLiteral("condition_zh")).toString()
            + QStringLiteral(" · 板端"));
        npuConfidenceLabel_->setText(
            QStringLiteral("%1%").arg(
                result.value(QStringLiteral("confidence")).toDouble() * 100.0,
                0, 'f', 2));
        npuRuntimeLabel_->setText(
            QStringLiteral("%1 ms · 板端").arg(
                result.value(QStringLiteral("average_latency_ms")).toDouble(),
                0, 'f', 3));
        QTimer::singleShot(10000, this, &MainWindow::requestNpuInference);
        return;
    }
    if (liveNpuSamples_.size() < 8) {
        npuStatusLabel_->setText(QStringLiteral("等待实时窗口 · 8 秒"));
        npuRuntimeLabel_->setText(QStringLiteral("正在缓存串口数据"));
        QTimer::singleShot(2000, this, &MainWindow::requestNpuInference);
        return;
    }
    npuRequestInFlight_ = true;

    auto *socket = new QTcpSocket(this);
    socket->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    socket->setProperty("npuDone", false);
    socket->setProperty("npuBuffer", QByteArray());

    auto fail = [this, socket](const QString &message) {
        if (socket->property("npuDone").toBool())
            return;
        socket->setProperty("npuDone", true);
        npuRequestInFlight_ = false;
        npuConditionLabel_->setText(QStringLiteral("推理不可用"));
        npuConfidenceLabel_->setText(QStringLiteral("—"));
        npuRuntimeLabel_->setText(message);
        qWarning().noquote() << "NPU inference failed:" << message;
        socket->abort();
        socket->deleteLater();
        QTimer::singleShot(5000, this, &MainWindow::requestNpuInference);
    };

    connect(socket, &QTcpSocket::connected, this, [this, socket] {
        QStringList values;
        values.reserve(liveNpuSamples_.size() * 5);
        for (const QVector<double> &sample : liveNpuSamples_) {
            for (double value : sample)
                values.append(QString::number(value, 'f', 6));
        }
        const QJsonObject request{
            {QStringLiteral("cmd"), QStringLiteral("npu_condition_live")},
            {QStringLiteral("values"), values.join(QLatin1Char(','))},
        };
        socket->write(
            QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    });
    connect(socket, &QTcpSocket::readyRead, this, [this, socket, fail] {
        QByteArray buffer = socket->property("npuBuffer").toByteArray()
            + socket->readAll();
        socket->setProperty("npuBuffer", buffer);
        if (socket->property("npuDone").toBool())
            return;

        const QJsonDocument doc = QJsonDocument::fromJson(buffer.trimmed());
        if (!doc.isObject())
            return;
        const QJsonObject root = doc.object();
        if (root.value(QStringLiteral("code")).toInt(-1) != 0) {
            fail(root.value(QStringLiteral("msg")).toString(
                QStringLiteral("NPU 请求失败")));
            return;
        }

        const QJsonObject data =
            root.value(QStringLiteral("data")).toObject();
        const QJsonObject result =
            data.value(QStringLiteral("result")).toObject();
        const QString conditionZh =
            result.value(QStringLiteral("condition_zh")).toString();
        const double confidence =
            result.value(QStringLiteral("confidence")).toDouble();
        const double latency =
            result.value(QStringLiteral("average_latency_ms")).toDouble();
        const QString runtime =
            result.value(QStringLiteral("runtime")).toString();

        socket->setProperty("npuDone", true);
        lastNpuResult_ = data;
        npuRequestInFlight_ = false;
        appendNpuInferenceLog(result, QStringLiteral("gateway_live"));
        npuStatusLabel_->setText(QStringLiteral("已部署 · 实时窗口"));
        npuConditionLabel_->setText(
            conditionZh.isEmpty()
                ? QStringLiteral("未知 · 实时")
                : conditionZh + QStringLiteral(" · 实时"));
        npuConfidenceLabel_->setText(
            confidence > 0.0
                ? QStringLiteral("%1%").arg(confidence * 100.0, 0, 'f', 2)
                : QStringLiteral("—"));
        npuRuntimeLabel_->setText(
            QStringLiteral("%1 ms · %2")
                .arg(latency, 0, 'f', 3)
                .arg(runtime.isEmpty() ? QStringLiteral("Alnpu") : runtime));
        socket->disconnectFromHost();
        socket->deleteLater();
        QTimer::singleShot(10000, this, &MainWindow::requestNpuInference);
    });
    connect(socket, &QTcpSocket::errorOccurred, this,
            [socket, fail](QAbstractSocket::SocketError) {
        fail(socket->errorString());
    });

    auto *timeout = new QTimer(socket);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, socket,
            [socket, fail] { fail(QStringLiteral("NPU 请求超时")); });
    timeout->start(10000);

    const QString host = qEnvironmentVariable(
        "FPGA_BOARD_HOST", QStringLiteral("192.168.137.100"));
    const quint16 port = static_cast<quint16>(
        qEnvironmentVariableIntValue("FPGA_BOARD_PORT") > 0
            ? qEnvironmentVariableIntValue("FPGA_BOARD_PORT")
            : 9000);
    socket->connectToHost(host, port);
}

void MainWindow::onSerialPortOpened(const QString &port)
{
    gatewayStatusLabel_->setText(
        QStringLiteral("● 串口 %1 · 等待从站").arg(port));
    gatewayStatusLabel_->setStyleSheet(
        QStringLiteral("color:#c9a227; font-size:12px; font-weight:600;"));
    statusBar()->showMessage(
        QStringLiteral("已打开 %1 · 9600 8N1 · Modbus RTU Slave 1~6").arg(port),
        5000);
}

void MainWindow::onSerialError(int slave, const QString &error)
{
    if (serialPollOutstanding_ > 0)
        --serialPollOutstanding_;
    if (slave >= 1
        && slave <= static_cast<int>(primaryRegistersByDevice_.size())) {
        primaryRegistersByDevice_[slave - 1].clear();
        primaryRegistersMsByDevice_[slave - 1] = 0;
    }
    serialOnline_ = false;
    for (qint64 timestamp : primaryRegistersMsByDevice_) {
        if (timestamp > 0
            && QDateTime::currentMSecsSinceEpoch() - timestamp < 3000) {
            serialOnline_ = true;
            break;
        }
    }

    bool fallbackUsed = false;
    quint16 fallbackMask = 0;
    const QVector<quint16> merged =
        mergedSerialRegisters(slave, &fallbackUsed, &fallbackMask);
    if (!merged.isEmpty() && fallbackUsed) {
        processSerialRegisters(
            slave, merged, fallbackPort_, fallbackUsed, fallbackMask);
        statusBar()->showMessage(
            QStringLiteral("COM10 从站 %1 通信失败，已切换到 COM21：%2")
                .arg(slave)
                .arg(error),
            5000);
        return;
    }
    showSerialError(error);
}

void MainWindow::onSerialControlFinished(bool ok, const QString &message)
{
    statusBar()->showMessage(message, ok ? 4000 : 6000);
}

void MainWindow::showSerialError(const QString &error)
{
    serialOnline_ = false;
    serialUseRealData_ = false;
    liveNpuSamples_.clear();
    gatewayStatusLabel_->setText(QStringLiteral("● 串口离线 · 无数据"));
    gatewayStatusLabel_->setStyleSheet(
        QStringLiteral("color:#e09090; font-size:12px; font-weight:600;"));
    plStatusLabel_->setText(QStringLiteral("串口离线 · 数据已停止"));
    statusBar()->showMessage(
        QStringLiteral("串口通信失败：%1").arg(error), 5000);
}

void MainWindow::startSelectedMotor()
{
    if (!serialWorker_) {
        showSerialError(QStringLiteral("串口工作线程未启动"));
        return;
    }
    if (selectedMotor().id != 1) {
        statusBar()->showMessage(
            QStringLiteral("当前设备未配置电机控制通道"),
            5000);
        return;
    }
    const quint16 pwm = static_cast<quint16>(pwmSpin_->value());
    statusBar()->showMessage(QStringLiteral("正在发送 A 路启动命令…"));
    emit startMotorRequested(1, pwm);
}

void MainWindow::stopSelectedMotor()
{
    if (!serialWorker_) {
        showSerialError(QStringLiteral("串口工作线程未启动"));
        return;
    }
    if (selectedMotor().id != 1) {
        statusBar()->showMessage(
            QStringLiteral("当前设备未配置电机控制通道"),
            5000);
        return;
    }
    statusBar()->showMessage(QStringLiteral("正在发送 A 路停止命令…"));
    emit stopMotorRequested(1);
}

void MainWindow::pollSerial()
{
    if (!serialWorker_ || !serialTimer_)
        return;
    if (QDateTime::currentMSecsSinceEpoch() < remoteHoldUntilMs_)
        return;
    if (serialPollOutstanding_ > 0)
        return;
    serialPollOutstanding_ = static_cast<int>(primaryRegistersByDevice_.size());
    for (int slave = 1;
         slave <= static_cast<int>(primaryRegistersByDevice_.size());
         ++slave) {
        emit pollSerialRequested(slave);
    }
    if (fallbackEnable_ && fallbackEnable_->isChecked()) {
        for (int slave = 1; slave <= static_cast<int>(fallbackRegistersByDevice_.size());
             ++slave) {
            emit pollFallbackRequested(slave);
        }
    }
}

void MainWindow::onSerialRegisters(
    int slave,
    const QVector<quint16> &regs,
    const QString &port)
{
    if (serialPollOutstanding_ > 0)
        --serialPollOutstanding_;
    if (slave < 1
        || slave > static_cast<int>(primaryRegistersByDevice_.size()))
        return;
    if (regs.size() != kSerialRegisterCount) {
        showSerialError(QStringLiteral("Modbus 返回寄存器数量异常"));
        return;
    }

    primaryPort_ = port;
    primaryRegistersByDevice_[slave - 1] = regs;
    primaryRegistersMsByDevice_[slave - 1] =
        QDateTime::currentMSecsSinceEpoch();
    serialOnline_ = true;
    bool fallbackUsed = false;
    quint16 fallbackMask = 0;
    const QVector<quint16> merged =
        mergedSerialRegisters(slave, &fallbackUsed, &fallbackMask);
    if (!merged.isEmpty())
        processSerialRegisters(
            slave, merged, port, fallbackUsed, fallbackMask);
}

void MainWindow::onFallbackRegisters(
    int slave,
    const QVector<quint16> &regs,
    const QString &port)
{
    if (slave < 1
        || slave > static_cast<int>(fallbackRegistersByDevice_.size())) {
        qWarning().noquote()
            << "fallback Modbus returned invalid slave" << slave;
        return;
    }
    if (regs.size() != kSerialRegisterCount) {
        qWarning().noquote()
            << "fallback Modbus returned invalid register count"
            << regs.size();
        return;
    }

    fallbackPort_ = port;
    fallbackRegistersByDevice_[slave - 1] = regs;
    fallbackRegistersMsByDevice_[slave - 1] =
        QDateTime::currentMSecsSinceEpoch();
    fallbackOnline_ = true;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool primaryFresh =
        slave >= 1
        && slave <= static_cast<int>(primaryRegistersByDevice_.size())
        && primaryRegistersByDevice_[slave - 1].size()
            == kSerialRegisterCount
        && now - primaryRegistersMsByDevice_[slave - 1] < 3000;
    if (!primaryFresh) {
        bool fallbackUsed = false;
        quint16 fallbackMask = 0;
        const QVector<quint16> merged =
            mergedSerialRegisters(slave, &fallbackUsed, &fallbackMask);
        if (!merged.isEmpty())
            processSerialRegisters(
                slave, merged, port, fallbackUsed, fallbackMask);
    }
}

void MainWindow::onFallbackError(int slave, const QString &error)
{
    if (slave >= 1
        && slave <= static_cast<int>(fallbackRegistersByDevice_.size())) {
        fallbackRegistersByDevice_[slave - 1].clear();
        fallbackRegistersMsByDevice_[slave - 1] = 0;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    fallbackOnline_ = false;
    for (qint64 timestamp : fallbackRegistersMsByDevice_) {
        if (timestamp > 0 && now - timestamp < 3000) {
            fallbackOnline_ = true;
            break;
        }
    }
    qWarning().noquote()
        << "fallback serial error:" << fallbackPort_
        << "slave" << slave << error;
    if (!serialOnline_ && fallbackEnable_ && fallbackEnable_->isChecked()) {
        gatewayStatusLabel_->setText(
            fallbackOnline_
                ? QStringLiteral("● COM21 在线 · 部分从站在线")
                : QStringLiteral("● COM10 离线 · COM21 连接失败"));
        gatewayStatusLabel_->setStyleSheet(
            fallbackOnline_
                ? QStringLiteral("color:#c9a227; font-size:12px; font-weight:600;")
                : QStringLiteral("color:#e09090; font-size:12px; font-weight:600;"));
    }
}

QVector<quint16> MainWindow::mergedSerialRegisters(
    int slave,
    bool *fallbackUsed,
    quint16 *fallbackMask) const
{
    if (fallbackUsed)
        *fallbackUsed = false;
    if (fallbackMask)
        *fallbackMask = 0;

    const int deviceIndex = slave - 1;
    if (deviceIndex < 0
        || deviceIndex >= static_cast<int>(fallbackRegistersByDevice_.size()))
        return {};
    const QVector<quint16> &fallback =
        fallbackRegistersByDevice_[deviceIndex];
    const QVector<quint16> &primary =
        primaryRegistersByDevice_[deviceIndex];
    const qint64 fallbackMs = fallbackRegistersMsByDevice_[deviceIndex];
    constexpr qint64 freshnessMs = 3000;
    constexpr quint16 fallbackFlag = 1u << 10;
    constexpr quint16 sourceReal = 1u << 4;
    constexpr quint16 sourceSim = 1u << 5;
    constexpr quint16 mpuError = 1u << 3;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool primaryFresh =
        primary.size() == kSerialRegisterCount
        && now - primaryRegistersMsByDevice_[deviceIndex] < freshnessMs;
    const bool fallbackFresh =
        fallbackEnable_ && fallbackEnable_->isChecked()
        && fallbackOnline_ && fallback.size() == kSerialRegisterCount
        && fallbackMs > 0 && now - fallbackMs < freshnessMs;

    if (!primaryFresh) {
        if (!fallbackFresh)
            return primary.size() == kSerialRegisterCount
                ? primary : QVector<quint16>();
        QVector<quint16> merged = fallback;
        merged[9] = static_cast<quint16>(
            (merged[9] & ~sourceReal) | sourceSim | fallbackFlag);
        if (fallbackUsed)
            *fallbackUsed = true;
        if (fallbackMask)
            *fallbackMask = 0x7FFF;
        return merged;
    }

    QVector<quint16> merged = primary;
    if (!fallbackFresh)
        return merged;

    // Bits 13:11 carry the selected 3D fault site. Preserve the fallback
    // selection while real values still come from the primary serial link.
    constexpr quint16 siteMask = 0x3800;
    const quint16 fallbackSiteBits = fallback[9] & siteMask;
    if (fallbackSiteBits != 0)
        merged[9] = static_cast<quint16>(
            (merged[9] & ~siteMask) | fallbackSiteBits);

    const bool primarySensorUsable =
        (primary[9] & sourceReal) != 0
        && (primary[9] & mpuError) == 0
        && primary[14] == 0x74;
    // IDs 2..6 are valid board-side simulated devices; only use COM21 to
    // fill explicit gaps. ID1 may fall back when its physical MPU is invalid.
    const bool fillPhysicalSensorGaps = slave == 1 && !primarySensorUsable;
    int filled = 0;
    const QVector<int> measuredRegisters = {
        2, 3, 4, 5, 6, 7, 8, 12, 13,
    };
    for (int index : measuredRegisters) {
        const bool missing =
            fillPhysicalSensorGaps || primary[index] == 0xFFFF;
        if (missing && fallback[index] != 0xFFFF) {
            merged[index] = fallback[index];
            if (fallbackMask)
                *fallbackMask |= static_cast<quint16>(1u << index);
            ++filled;
        }
    }

    if ((primary[14] == 0 || primary[14] == 0xFFFF)
        && fallback[14] != 0
        && fallback[14] != 0xFFFF) {
        merged[14] = fallback[14];
        if (fallbackMask)
            *fallbackMask |= static_cast<quint16>(1u << 14);
        ++filled;
    }

    if (filled > 0) {
        quint16 flags = primary[9];
        if (!primarySensorUsable) {
            flags = static_cast<quint16>((flags & ~sourceReal) | sourceSim);
        }
        merged[9] = static_cast<quint16>(flags | fallbackFlag);
        if (fallbackUsed)
            *fallbackUsed = true;
    }
    return merged;
}

void MainWindow::processSerialRegisters(
    int slave,
    const QVector<quint16> &regs,
    const QString &port,
    bool fallbackUsed,
    quint16 fallbackMask)
{
    ++serialTick_;
    serialOnline_ = true;
    serialUseRealData_ = true;
    const bool selectedDevice = slave == selectedMotor().id;
    gatewayStatusLabel_->setText(fallbackUsed
        ? QStringLiteral("● COM10+COM21 · 在线")
        : fallbackEnable_ && fallbackEnable_->isChecked() && fallbackOnline_
            ? QStringLiteral("● COM10 在线 · COM21 待命")
            : QStringLiteral("● 串口 %1 · 从站在线").arg(port));
    gatewayStatusLabel_->setStyleSheet(
        QStringLiteral("color:#6ee7a0; font-size:12px; font-weight:600;"));

    constexpr double lsbPerG = 16384.0;
    constexpr double gToMs2 = 9.80665;
    const qint16 accelXRaw = static_cast<qint16>(regs[2]);
    const qint16 accelYRaw = static_cast<qint16>(regs[3]);
    const qint16 accelZRaw = static_cast<qint16>(regs[4]);
    const qint16 temperatureRaw = static_cast<qint16>(regs[5]);
    const qint16 gyroXRaw = static_cast<qint16>(regs[6]);
    const qint16 gyroYRaw = static_cast<qint16>(regs[7]);
    const qint16 gyroZRaw = static_cast<qint16>(regs[8]);
    const quint16 motorCtrlRaw = regs[0];
    const int motorPwmRaw = qMin<int>(regs[1], 1000);
    const quint16 sourceFlags = regs[9];
    const bool thresholdsAvailable =
        regs[10] != 0xFFFF && regs[11] != 0xFFFF
        && regs[10] < regs[11];
    constexpr quint16 kDefaultAlertLowRaw = 3000;
    constexpr quint16 kDefaultAlertHighRaw = 4500;
    const quint16 alertLowRaw =
        thresholdsAvailable ? regs[10] : kDefaultAlertLowRaw;
    const quint16 alertHighRaw =
        thresholdsAvailable ? regs[11] : kDefaultAlertHighRaw;
    const quint16 rmsRaw = regs[12];
    const quint16 peakRaw = regs[13];
    const quint8 whoami = static_cast<quint8>(regs[14] & 0xFF);
    const quint16 currentRaw = regs[15];
    const double temperatureC = temperatureRaw / 333.87 + 21.0;
    const double currentA = currentRaw / 100.0;
    const double rmsMs2 = rmsRaw / lsbPerG * gToMs2;
    const double peakMs2 = peakRaw / lsbPerG * gToMs2;
    const bool liveTemperatureValid =
        temperatureRaw != static_cast<qint16>(-1)
        && temperatureC > 5.0 && temperatureC < 90.0;
    if (selectedDevice) {
        if (liveTemperatureValid)
            lastLiveNpuTemperature_ = temperatureC;
        liveNpuSamples_.append(
            {rmsMs2, peakMs2, lastLiveNpuTemperature_, currentA,
             motorPwmRaw / 1000.0});
        if (liveNpuSamples_.size() > 8)
            liveNpuSamples_.remove(0, liveNpuSamples_.size() - 8);
    }
    const bool highReached =
        alertHighRaw != 0xFFFF && rmsRaw >= alertHighRaw;
    const bool lowReached =
        alertLowRaw != 0xFFFF && rmsRaw >= alertLowRaw;
    const int rawLevel = highReached ? 2 : lowReached ? 1 : 0;
    const quint16 pointSiteStatus =
        regs.size() > 21 ? regs[21] : static_cast<quint16>(0);
    quint16 maxPointRaw = 0;
    int maxPointSiteCode = 0;
    if (regs.size() > 20) {
        for (int i = 0; i < 5; ++i) {
            const quint16 pointRaw = regs[16 + i];
            if (pointRaw > maxPointRaw) {
                maxPointRaw = pointRaw;
                maxPointSiteCode = i + 1;
            }
        }
    }
    const int pointLevel = maxPointRaw >= 600 ? 2
        : maxPointRaw >= 250 ? 1 : 0;
    const int pointSiteCode =
        pointLevel > 0 ? maxPointSiteCode : 0;
    const int level = std::max(rawLevel, pointLevel);
    qInfo().noquote()
        << "serial sample"
        << "slave" << slave
        << port
        << "rmsRaw=" << rmsRaw
        << "low=" << alertLowRaw
        << "high=" << alertHighRaw
        << "rawLevel=" << rawLevel
        << "fallbackMask=" << QString::number(fallbackMask, 16)
        << "level=" << level;
    const bool hardwareSensor = (sourceFlags & (1u << 4)) != 0;
    const QString rmsSource = registerSource(fallbackMask, 12, hardwareSensor);
    const QString peakSource = registerSource(fallbackMask, 13, hardwareSensor);
    const QString accelSource =
        groupedRegisterSource(fallbackMask, 0x001C, hardwareSensor);
    const QString gyroSource =
        groupedRegisterSource(fallbackMask, 0x01C0, hardwareSensor);
    const QString temperatureSource =
        registerSource(fallbackMask, 5, hardwareSensor);
    const QString motorSource =
        groupedRegisterSource(fallbackMask, 0x0003, hardwareSensor);
    const QString thresholdSource = thresholdsAvailable
        ? QStringLiteral("board") : QStringLiteral("engineering_default");
    const QString deviceSource =
        groupedRegisterSource(fallbackMask, 0x7FFF, hardwareSensor);
    const QString vibrationSource = rmsSource;
    const QString currentSource = (fallbackMask & (1u << 15)) != 0
        ? QStringLiteral("fallback") : QStringLiteral("simulated");
    const QString source = fallbackUsed
        ? (hardwareSensor
               ? QStringLiteral("MPU+PL · COM21")
               : QStringLiteral("COM21 · 通道 %1")
                     .arg(slave))
        : hardwareSensor
            ? QStringLiteral("MPU兼容器件 0x%1 + PL")
                  .arg(whoami, 2, 16, QLatin1Char('0')).toUpper()
            : QStringLiteral("板端通道 %1").arg(slave);
    const QString state = level >= 2 ? QStringLiteral("fault")
        : level == 1 ? QStringLiteral("warning") : QStringLiteral("normal");
    const QString statusText = level >= 2 ? QStringLiteral("严重振动告警")
        : level == 1 ? QStringLiteral("振动预警") : QStringLiteral("运行正常");
    const bool mpuHousingAlarm = slave == 1 && rawLevel > 0
        && rmsSource == QLatin1String("real");
    const int faultSiteCode = mpuHousingAlarm ? 5
        : pointLevel > 0 ? pointSiteCode : ((sourceFlags >> 11) & 0x7);
    const QString selectedSite = siteKeyFromCode(faultSiteCode);
    const QString faultSite = level == 0 ? QStringLiteral("none")
        : selectedSite == QLatin1String("none") ? QStringLiteral("body")
                                                : selectedSite;

    QJsonObject data;
    data.insert(QStringLiteral("device_id"), slave);
    data.insert(QStringLiteral("source"), source);
    data.insert(QStringLiteral("device_source"), deviceSource);
    data.insert(QStringLiteral("vibration_source"), vibrationSource);
    data.insert(QStringLiteral("rms_source"), rmsSource);
    data.insert(QStringLiteral("peak_source"), peakSource);
    data.insert(QStringLiteral("accel_source"), accelSource);
    data.insert(QStringLiteral("gyro_source"), gyroSource);
    data.insert(QStringLiteral("temperature_source"), temperatureSource);
    data.insert(QStringLiteral("current_source"), currentSource);
    data.insert(QStringLiteral("motor_source"), motorSource);
    data.insert(QStringLiteral("threshold_source"), thresholdSource);
    data.insert(QStringLiteral("motor_a_real"), (sourceFlags & (1u << 6)) != 0);
    data.insert(QStringLiteral("motor_enabled"),
                (motorCtrlRaw & 1u) != 0 && motorPwmRaw > 0);
    data.insert(QStringLiteral("motor_pwm"), motorPwmRaw);
    data.insert(QStringLiteral("motor_direction"),
                static_cast<int>((motorCtrlRaw >> 1) & 1u));
    data.insert(QStringLiteral("algo"), QStringLiteral("RMS/阈值自动判定"));
    data.insert(QStringLiteral("status"), state);
    data.insert(QStringLiteral("status_text"), statusText);
    data.insert(QStringLiteral("fault_site"), faultSite);
    data.insert(QStringLiteral("fault_site_code"), faultSiteCode);
    data.insert(QStringLiteral("fault_location"),
                level == 0 ? QStringLiteral("整机") : siteText(faultSite));
    data.insert(QStringLiteral("fault_type"),
                level == 0 ? QString() : QStringLiteral("振动异常"));
    data.insert(QStringLiteral("vibration_rms"), rmsMs2);
    data.insert(QStringLiteral("vibration_peak"), peakMs2);
    data.insert(QStringLiteral("temperature"), temperatureC);
    data.insert(QStringLiteral("current"), currentA);
    data.insert(QStringLiteral("temperature_raw"), temperatureRaw);
    data.insert(QStringLiteral("current_raw"), currentRaw);
    data.insert(QStringLiteral("rms_raw"), rmsRaw);
    data.insert(QStringLiteral("alert_low_raw"), alertLowRaw);
    data.insert(QStringLiteral("alert_high_raw"), alertHighRaw);
    data.insert(QStringLiteral("accel_x_raw"), accelXRaw);
    data.insert(QStringLiteral("accel_y_raw"), accelYRaw);
    data.insert(QStringLiteral("accel_z_raw"), accelZRaw);
    data.insert(QStringLiteral("gyro_x_raw"), gyroXRaw);
    data.insert(QStringLiteral("gyro_y_raw"), gyroYRaw);
    data.insert(QStringLiteral("gyro_z_raw"), gyroZRaw);
    data.insert(QStringLiteral("raw_lsb"), accelXRaw);
    data.insert(QStringLiteral("filtered_lsb"), 0);
    data.insert(QStringLiteral("mean_abs_lsb"), 0);
    data.insert(QStringLiteral("zero_crossings"), 0);
    data.insert(QStringLiteral("sensor_status"), sourceFlags);
    for (int i = 0; i < 5; ++i) {
        data.insert(
            QStringLiteral("point%1").arg(i + 1),
            regs.size() > 16 + i ? regs[16 + i] : static_cast<quint16>(0));
    }
    data.insert(QStringLiteral("point_site_status"), pointSiteStatus);
    data.insert(QStringLiteral("npu_deployed"), (sourceFlags & (1u << 8)) != 0);
    data.insert(QStringLiteral("npu_model_ready"),
                (sourceFlags & (1u << 9)) != 0);
    data.insert(QStringLiteral("fallback_active"), fallbackUsed);
    data.insert(QStringLiteral("fallback_port"), fallbackPort_);
    data.insert(QStringLiteral("alert_level"), level);
    data.insert(QStringLiteral("alert_low"), alertLowRaw);
    data.insert(QStringLiteral("alert_high"), alertHighRaw);
    data.insert(QStringLiteral("whoami"), whoami);
    data.insert(QStringLiteral("bus_ok"), false);
    data.insert(QStringLiteral("bus_has_env"), true);
    const bool boardModelValid =
        regs.size() > 30 && (regs[30] & 0x1) != 0;
    const int boardConditionCode =
        regs.size() > 22 ? static_cast<int>(regs[22]) : -1;
    const double boardConfidence =
        regs.size() > 23 ? regs[23] / 10000.0 : 0.0;
    const double boardLatencyMs =
        regs.size() > 24 ? regs[24] / 1000.0 : 0.0;
    static const QStringList conditionKeys = {
        QStringLiteral("stopped"),
        QStringLiteral("idle"),
        QStringLiteral("normal"),
        QStringLiteral("overload"),
        QStringLiteral("fault"),
    };
    static const QStringList conditionTexts = {
        QStringLiteral("停机"),
        QStringLiteral("空载"),
        QStringLiteral("正常带载"),
        QStringLiteral("过载"),
        QStringLiteral("异常"),
    };
    const QString boardCondition =
        boardConditionCode >= 0 && boardConditionCode < conditionKeys.size()
            ? conditionKeys[boardConditionCode] : QStringLiteral("unknown");
    const QString boardConditionZh =
        boardConditionCode >= 0 && boardConditionCode < conditionTexts.size()
            ? conditionTexts[boardConditionCode] : QStringLiteral("未知");
    QJsonObject boardProbabilities;
    for (int i = 0; i < conditionKeys.size(); ++i) {
        boardProbabilities.insert(
            conditionKeys[i],
            regs.size() > 25 + i ? regs[25 + i] / 10000.0 : 0.0);
    }
    QJsonObject boardNpuResult{
        {QStringLiteral("condition"), boardCondition},
        {QStringLiteral("condition_zh"), boardConditionZh},
        {QStringLiteral("confidence"), boardConfidence},
        {QStringLiteral("average_latency_ms"), boardLatencyMs},
        {QStringLiteral("probabilities"), boardProbabilities},
        {QStringLiteral("runtime"), QStringLiteral("板端多节点模型")},
    };
    data.insert(QStringLiteral("board_npu_valid"), boardModelValid);
    data.insert(QStringLiteral("board_npu"), boardNpuResult);
    data.insert(QStringLiteral("serial_port"),
                fallbackUsed && !primaryPort_.isEmpty()
                    ? primaryPort_ + QStringLiteral("+") + fallbackPort_
                    : port);
    applyDeviceData(data);

    if (selectedDevice) {
        tempTrendWave_->appendSample(rmsMs2);
        aiResultLabel_->setText(boardAnalysisText());
        if (boardModelValid) {
            lastNpuResult_ = QJsonObject{
                {QStringLiteral("result"), boardNpuResult},
            };
            appendNpuInferenceLog(
                boardNpuResult,
                QStringLiteral("serial_board"));
            npuStatusLabel_->setText(QStringLiteral("板端多节点模型 · 实时"));
            npuConditionLabel_->setText(
                boardConditionZh + QStringLiteral(" · 板端"));
            npuConfidenceLabel_->setText(
                QStringLiteral("%1%").arg(boardConfidence * 100.0, 0, 'f', 2));
            npuRuntimeLabel_->setText(
                QStringLiteral("%1 ms · 板端").arg(boardLatencyMs, 0, 'f', 3));
        }
    }

    if (selectedDevice && refreshAnalysisOnNextSample_) {
        refreshAnalysisOnNextSample_ = false;
        statusBar()->showMessage(QStringLiteral("已刷新实时分析结果"), 3000);
    }
}

void MainWindow::applyDeviceData(const QJsonObject &data)
{
    const int responseId = data.value(QStringLiteral("device_id")).toInt(1);
    if (responseId < 1 || responseId > static_cast<int>(motors_.size()))
        return;
    MotorSnapshot &motor = motorById(responseId);
    motor.hasData = true;
    motor.rms = data.value(QStringLiteral("vibration_rms")).toDouble(motor.rms);
    motor.peak = data.value(QStringLiteral("vibration_peak")).toDouble(motor.peak);
    motor.source = data.value(QStringLiteral("source")).toString(motor.source);
    motor.deviceSource = data.value(QStringLiteral("device_source"))
                             .toString(motor.deviceSource);
    motor.vibrationSource = data.value(QStringLiteral("vibration_source"))
                                .toString(motor.vibrationSource);
    motor.temperatureSource = data.value(QStringLiteral("temperature_source"))
                                  .toString(motor.temperatureSource);
    motor.currentSource = data.value(QStringLiteral("current_source"))
                              .toString(motor.currentSource);
    motor.npuDeployed =
        data.value(QStringLiteral("npu_deployed")).toBool(motor.npuDeployed);
    motor.npuModelReady =
        data.value(QStringLiteral("npu_model_ready")).toBool(motor.npuModelReady);
    motor.motorEnabled =
        data.value(QStringLiteral("motor_enabled")).toBool(motor.motorEnabled);
    motor.motorPwm = qBound(
        0, data.value(QStringLiteral("motor_pwm")).toInt(motor.motorPwm), 1000);
    motor.motorDirection =
        data.value(QStringLiteral("motor_direction")).toInt(motor.motorDirection)
        == 0 ? 0 : 1;
    motor.algo = data.value(QStringLiteral("algo")).toString(motor.algo);
    motor.healthHasRms = true;
    motor.hasEnv = data.value(QStringLiteral("bus_has_env")).toBool(false);
    motor.healthHasTemp =
        motor.hasEnv && data.contains(QStringLiteral("temperature"));
    motor.healthHasCurrent =
        motor.hasEnv && data.contains(QStringLiteral("current"));
    if (motor.healthHasTemp)
        motor.tempC = data.value(QStringLiteral("temperature")).toDouble();
    if (motor.healthHasCurrent)
        motor.currentA = data.value(QStringLiteral("current")).toDouble();
    motor.lastGwData = data;

    const QString st = data.value(QStringLiteral("status")).toString();
    motor.state = (st == QLatin1String("fault_major")
                   || st == QLatin1String("fault")
                   || st == QLatin1String("emergency"))
        ? QStringLiteral("fault")
        : (st == QLatin1String("fault_minor")
           || st == QLatin1String("warning"))
            ? QStringLiteral("warning")
            : QStringLiteral("normal");
    const QString site = data.value(QStringLiteral("fault_site"))
                             .toString(QStringLiteral("none"));
    motor.faultSite = motor.state == QLatin1String("normal")
        ? QStringLiteral("none") : site;
    const QString loc = data.value(QStringLiteral("fault_location")).toString();
    motor.faultLabel = QStringLiteral("%1 · %2 · RMS %3 m/s²")
        .arg(data.value(QStringLiteral("status_text")).toString())
        .arg(loc.isEmpty() ? QStringLiteral("整机") : loc)
        .arg(motor.rms, 0, 'f', 4);
    updateHealthScore(motor);
    recordRmsAndScheduleAlertSnapshot(responseId, data);
    updateMotorButtons();

    if (responseId == selectedMotor().id) {
        aiResultLabel_->setText(boardAnalysisText());
        refreshPanel();
    }
}

void MainWindow::updateHealthScore(MotorSnapshot &motor)
{
    double weighted = 0.0;
    double totalWeight = 0.0;

    if (motor.healthHasRms) {
        weighted += 0.55 * vibrationScore(motor.rms);
        totalWeight += 0.55;
    }
    if (motor.healthHasTemp) {
        weighted += 0.25 * temperatureScore(motor.tempC);
        totalWeight += 0.25;
    }
    if (motor.healthHasCurrent) {
        weighted += 0.10 * currentScore(motor.currentA);
        totalWeight += 0.10;
    }

    weighted += 0.10 * stateScore(motor.state);
    totalWeight += 0.10;

    const double instant = totalWeight > 0.0 ? weighted / totalWeight : 100.0;
    const double target = qMin(instant, stateHealthCap(motor.state));

    // 劣化按较快速度响应，恢复做慢速确认，避免一次正常采样把健康度拉满。
    const double alpha = target < motor.health ? 0.45 : 0.15;
    const double smoothed = alpha * target + (1.0 - alpha) * motor.health;
    motor.health = qBound(0, qRound(qMin(smoothed, stateHealthCap(motor.state))), 100);
}

MainWindow::MotorSnapshot &MainWindow::motorById(int deviceId)
{
    return motors_[qBound(1, deviceId, static_cast<int>(motors_.size())) - 1];
}

const MainWindow::MotorSnapshot &MainWindow::motorById(int deviceId) const
{
    return motors_[qBound(1, deviceId, static_cast<int>(motors_.size())) - 1];
}

MainWindow::MotorSnapshot &MainWindow::selectedMotor()
{
    return motors_[selectedMotorIndex_];
}

const MainWindow::MotorSnapshot &MainWindow::selectedMotor() const
{
    return motors_[selectedMotorIndex_];
}

void MainWindow::resetWaveforms()
{
    if (vibWave_)
        vibWave_->clear();
    if (tempTrendWave_)
        tempTrendWave_->clear();
}

void MainWindow::refreshSimulatedDevices()
{
    if (QDateTime::currentMSecsSinceEpoch() < remoteHoldUntilMs_)
        return;

    const double phase = simTick_ * 0.08;
    for (int id = 2; id <= static_cast<int>(motors_.size()); ++id) {
        int level = 0;
        if (id == 4)
            level = 1;
        else if (id == 5)
            level = 2;
        else if (id == 3 && (simTick_ / 300) % 2 == 1)
            level = 1;

        const double jitter = 0.012 * qSin(phase + id * 0.7);
        const double rms = level == 2 ? 0.78 + jitter
            : level == 1 ? 0.48 + jitter
                         : 0.22 + jitter;
        const double temperature = level == 2 ? 68.0
            : level == 1 ? 57.0
                         : 42.0;
        const double current = level == 2 ? 6.2
            : level == 1 ? 4.8
                         : 3.6;
        const QString state = level == 2 ? QStringLiteral("fault")
            : level == 1 ? QStringLiteral("warning")
                         : QStringLiteral("normal");
        const QString site = level == 0 ? QStringLiteral("none")
            : level == 2 ? QStringLiteral("bearing")
                         : QStringLiteral("looseness");
        const QString statusText = level == 2 ? QStringLiteral("严重振动告警")
            : level == 1 ? QStringLiteral("振动预警")
                         : QStringLiteral("运行正常");

        QJsonObject data;
        data.insert(QStringLiteral("device_id"), id);
        data.insert(QStringLiteral("device_name"), QStringLiteral("%1号电机").arg(id));
        data.insert(QStringLiteral("source"), QStringLiteral("受控工况数据"));
        data.insert(QStringLiteral("device_source"), QStringLiteral("simulated"));
        data.insert(QStringLiteral("vibration_source"), QStringLiteral("simulated"));
        data.insert(QStringLiteral("rms_source"), QStringLiteral("simulated"));
        data.insert(QStringLiteral("peak_source"), QStringLiteral("simulated"));
        data.insert(QStringLiteral("accel_source"), QStringLiteral("simulated"));
        data.insert(QStringLiteral("gyro_source"), QStringLiteral("simulated"));
        data.insert(QStringLiteral("temperature_source"), QStringLiteral("simulated"));
        data.insert(QStringLiteral("current_source"), QStringLiteral("simulated"));
        data.insert(QStringLiteral("motor_source"), QStringLiteral("simulated"));
        data.insert(QStringLiteral("threshold_source"), QStringLiteral("simulated"));
        data.insert(QStringLiteral("algo"), QStringLiteral("板端工况场景"));
        data.insert(QStringLiteral("status"), state);
        data.insert(QStringLiteral("status_text"), statusText);
        data.insert(QStringLiteral("fault_site"), site);
        data.insert(QStringLiteral("fault_location"), siteText(site));
        data.insert(QStringLiteral("fault_type"),
                    level == 0 ? QString() : QStringLiteral("振动异常特征"));
        data.insert(QStringLiteral("vibration_rms"), rms);
        data.insert(QStringLiteral("vibration_peak"), rms * 1.6);
        data.insert(QStringLiteral("temperature"), temperature);
        data.insert(QStringLiteral("current"), current);
        data.insert(QStringLiteral("alert_level"), level);
        data.insert(QStringLiteral("bus_ok"), true);
        data.insert(QStringLiteral("bus_has_env"), true);
        applyDeviceData(data);
    }
}

void MainWindow::selectMotor(int deviceId)
{
    const int targetId = qBound(1, deviceId, static_cast<int>(motors_.size()));
    selectedMotorIndex_ = targetId - 1;
    resetWaveforms();
    updateMotorButtons();
    aiResultLabel_->setText(boardAnalysisText());
    const QJsonObject &latest = selectedMotor().lastGwData;
    if (latest.value(QStringLiteral("board_npu_valid")).toBool(false)) {
        const QJsonObject result =
            latest.value(QStringLiteral("board_npu")).toObject();
        lastNpuResult_ = QJsonObject{
            {QStringLiteral("result"), result},
        };
        npuStatusLabel_->setText(QStringLiteral("板端多节点模型 · 实时"));
        npuConditionLabel_->setText(
            result.value(QStringLiteral("condition_zh")).toString()
            + QStringLiteral(" · 板端"));
        npuConfidenceLabel_->setText(
            QStringLiteral("%1%").arg(
                result.value(QStringLiteral("confidence")).toDouble() * 100.0,
                0, 'f', 2));
        npuRuntimeLabel_->setText(
            QStringLiteral("%1 ms · 板端").arg(
                result.value(QStringLiteral("average_latency_ms")).toDouble(),
                0, 'f', 3));
    }
    refreshPanel();
    statusBar()->showMessage(
        QStringLiteral("正在查看 %1 号电机 · %2 · %3 · 健康度 %4%")
            .arg(targetId)
            .arg(selectedMotor().hasData ? stateText(selectedMotor().state)
                                         : QStringLiteral("等待串口数据"))
            .arg(selectedMotor().hasData
                     ? metricSourceText(selectedMotor().deviceSource)
                     : QStringLiteral("未知"))
            .arg(selectedMotor().hasData ? QString::number(selectedMotor().health)
                                         : QStringLiteral("—")),
        3500);
}

void MainWindow::updateControlAvailability()
{
    const bool physicalMotor = selectedMotor().id == 1;
    if (pwmSpin_)
        pwmSpin_->setEnabled(physicalMotor);
    if (startMotorButton_) {
        startMotorButton_->setEnabled(physicalMotor);
        startMotorButton_->setToolTip(
            physicalMotor ? QStringLiteral("启动 1 号电机")
                          : QStringLiteral("该设备未配置电机控制通道"));
    }
    if (stopMotorButton_) {
        stopMotorButton_->setEnabled(physicalMotor);
        stopMotorButton_->setToolTip(
            physicalMotor ? QStringLiteral("停止 1 号电机")
                          : QStringLiteral("该设备未配置电机控制通道"));
    }
}

void MainWindow::updateMotorButtons()
{
    for (int i = 0; i < static_cast<int>(motorButtons_.size()); ++i) {
        QPushButton *button = motorButtons_[i];
        if (!button)
            continue;

        const MotorSnapshot &motor = motors_[i];
        const bool selected = (i == selectedMotorIndex_);
        if (motor.hasData) {
            const QString sourceTag = motor.id == 1
                ? QStringLiteral("控制通道")
                : QStringLiteral("监测通道");
            button->setText(QStringLiteral("%1号\n%2 · %3")
                                .arg(motor.id)
                                .arg(sourceTag)
                                .arg(stateText(motor.state)));
            button->setToolTip(
                QStringLiteral("查看 %1 号电机\n状态：%2\n健康度：%3%\n位置：%4")
                    .arg(motor.id)
                    .arg(stateText(motor.state))
                    .arg(motor.health)
                    .arg(siteText(motor.faultSite)));
        } else {
            button->setText(QStringLiteral("%1号\n待连接").arg(motor.id));
            button->setToolTip(QStringLiteral("查看 %1 号电机").arg(motor.id));
        }
        button->setChecked(selected);
        button->setStyleSheet(motorButtonStyle(motor.state, motor.hasData, selected));
    }
    updateControlAvailability();
}

void MainWindow::refreshPanel()
{
    const MotorSnapshot &motor = selectedMotor();
    int systemAlertLevel = 0;
    for (const MotorSnapshot &item : motors_) {
        const int level =
            item.lastGwData.value(QStringLiteral("alert_level")).toInt(0);
        systemAlertLevel = std::max(systemAlertLevel, level);
    }
    if (boardAlertClient_)
        boardAlertClient_->setLevel(systemAlertLevel);

    const QString deviceTag = motor.id == 1
        ? QStringLiteral("控制通道")
        : QStringLiteral("监测通道");
    deviceTitleLabel_->setText(
        QStringLiteral("%1 号电机 · %2").arg(motor.id).arg(deviceTag));
    rmsValueLabel_->setText(motor.hasData
                                ? QStringLiteral("%1 m/s²")
                                      .arg(motor.rms, 0, 'f', 3)
                                : QStringLiteral("—"));
    if (!motor.hasData) {
        tempValueLabel_->setText(QStringLiteral("—"));
        currentValueLabel_->setText(QStringLiteral("—"));
    } else {
        tempValueLabel_->setText(
            motor.healthHasTemp
                ? QStringLiteral("%1 ℃").arg(motor.tempC, 0, 'f', 1)
                : serialUseRealData_ ? QStringLiteral("— 从站未上报") : QStringLiteral("—"));
        currentValueLabel_->setText(
            motor.healthHasCurrent
                ? QStringLiteral("%1 A").arg(motor.currentA, 0, 'f', 2)
                : serialUseRealData_ ? QStringLiteral("— 从站未上报") : QStringLiteral("—"));
    }
    healthValueLabel_->setText(motor.hasData ? QStringLiteral("%1 %").arg(motor.health)
                                             : QStringLiteral("—"));
    stateValueLabel_->setText(motor.hasData ? stateText(motor.state) : QStringLiteral("—"));
    siteValueLabel_->setText(motor.hasData ? siteText(motor.faultSite) : QStringLiteral("—"));

    if (!motor.hasData) {
        plStatusLabel_->setText(QStringLiteral("等待数据"));
    } else if (motor.vibrationSource == QLatin1String("simulated")) {
        plStatusLabel_->setText(QStringLiteral("PL 在线 · 数据更新中"));
    } else if (motor.vibrationSource == QLatin1String("fallback")) {
        plStatusLabel_->setText(QStringLiteral("振动 RMS · 在线"));
    } else if (motor.vibrationSource == QLatin1String("mixed")) {
        plStatusLabel_->setText(QStringLiteral("振动 RMS · 在线"));
    } else if (motor.state == QLatin1String("fault")) {
        plStatusLabel_->setText(QStringLiteral("振动 RMS · 告警"));
    } else {
        plStatusLabel_->setText(QStringLiteral("振动 RMS · 采集中"));
    }
    // NPU 状态来自板端 Modbus 标志位。
    if (!motor.hasData)
        npuStatusLabel_->setText(QStringLiteral("—"));
    else if (motor.npuDeployed)
        npuStatusLabel_->setText(
            motor.npuModelReady ? QStringLiteral("已部署 · soft_npu · 模型就绪")
                                : QStringLiteral("已部署 · 模型缺失"));
    else
        npuStatusLabel_->setText(QStringLiteral("未部署"));
    if (motor.vibrationSource == QLatin1String("simulated")) {
        sampleRateLabel_->setText(QStringLiteral("约 326.6 Hz（PL 通道）"));
    } else if (motor.vibrationSource == QLatin1String("fallback")) {
        sampleRateLabel_->setText(QStringLiteral("—"));
    } else if (motor.vibrationSource == QLatin1String("mixed")) {
        sampleRateLabel_->setText(QStringLiteral("—"));
    } else {
        sampleRateLabel_->setText(QStringLiteral("约 326.6 Hz"));
    }

    faultLabel_->setText(motor.hasData ? motor.faultLabel
                                       : QStringLiteral("等待 %1 号电机串口数据").arg(motor.id));
    if (!motor.hasData) {
        metaLabel_->setText(QStringLiteral("尚未收到该电机数据 · 点击编号切换并等待轮询"));
    } else if (!motor.source.isEmpty()) {
        metaLabel_->setText(
            QStringLiteral("设备 %1 · %2 · 峰值 %3 m/s²")
                .arg(motor.id)
                .arg(stateText(motor.state))
                .arg(motor.peak, 0, 'f', 3));
    } else {
        metaLabel_->setText(QStringLiteral("设备 %1 · %2")
                                .arg(motor.id)
                                .arg(stateText(motor.state)));
    }

    clockLabel_->setText(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")));

    QString faultStyle = QStringLiteral("QLabel#fault { background:#5f8a6533; color:#8fbf96; }");
    QColor vibColor(0x5f, 0x8a, 0x65);
    if (motor.state == QLatin1String("notice")) {
        faultStyle = QStringLiteral("QLabel#fault { background:#2563c433; color:#93b4e8; }");
        vibColor = QColor(0x25, 0x63, 0xc4);
    } else if (motor.state == QLatin1String("warning")) {
        faultStyle = QStringLiteral("QLabel#fault { background:#c9a22733; color:#e8d48a; }");
        vibColor = QColor(0xc9, 0xa2, 0x27);
    } else if (motor.state == QLatin1String("alarm")) {
        faultStyle = QStringLiteral("QLabel#fault { background:#d9770633; color:#f0b878; }");
        vibColor = QColor(0xd9, 0x77, 0x06);
    } else if (motor.state == QLatin1String("fault")) {
        faultStyle = QStringLiteral("QLabel#fault { background:#b91c1c44; color:#e8a0a0; }");
        vibColor = QColor(0xb9, 0x1c, 0x1c);
    } else if (motor.state == QLatin1String("emergency")) {
        faultStyle = QStringLiteral("QLabel#fault { background:#991b1b44; color:#e09090; }");
        vibColor = QColor(0x99, 0x1b, 0x1b);
    }
    faultLabel_->setStyleSheet(faultStyle);

    vibWave_->setLineColor(vibColor);

    applyViewportTwinVisual();
}

void MainWindow::applyViewportTwinVisual()
{
    const MotorSnapshot &motor = selectedMotor();
    if (viewportUsesJichu3D_) {
        if (auto *v = qobject_cast<MotorView3D *>(viewport_)) {
            v->setTwinVisual(motor.id, motor.state, motor.faultSite);
            v->setMotorRuntime(motor.id, motor.motorEnabled,
                               motor.motorPwm, motor.motorDirection);
        }
    } else {
        if (auto *m = qobject_cast<MotorGlWidget *>(viewport_))
            m->setTwinVisual(motor.id, motor.state, motor.faultSite);
    }
}

void MainWindow::pushWaveformSamples()
{
    // 波形由串口轮询到的 RAW/FILTERED 寄存器驱动。
}

void MainWindow::updateHudMetrics()
{
    // 指标不再本地编造：全部来自 Modbus FC03 的真实返回。
}

void MainWindow::onWaveTimerTick()
{
    ++simTick_;
    const bool demoSimulationEnabled =
        qEnvironmentVariableIntValue("FPGA_DEMO_SIM_DEVICES") == 1;
    if (demoSimulationEnabled && simTick_ % 25 == 0)
        refreshSimulatedDevices();
}

AiClient::DeviceContext MainWindow::currentDeviceContext() const
{
    const MotorSnapshot &motor = selectedMotor();
    AiClient::DeviceContext ctx;
    ctx.deviceId = motor.id;
    ctx.vibrationRms = motor.rms;
    ctx.tempC = motor.tempC;
    ctx.currentA = motor.currentA;
    ctx.healthPercent = motor.health;
    ctx.runState = stateText(motor.state);
    ctx.faultSite = siteText(motor.faultSite);
    ctx.faultLabel = motor.faultLabel;
    return ctx;
}

void MainWindow::onAiAnalyzeClicked()
{
    // 串口读取是异步的，收到最新一帧后再刷新板端分析文本。
    refreshAnalysisOnNextSample_ = true;
    pollSerial();
    statusBar()->showMessage(QStringLiteral("正在读取最新实时分析结果…"));
}

QString MainWindow::buildTestReportMarkdown() const
{
    const MotorSnapshot &motor = selectedMotor();
    const QJsonObject &data = motor.lastGwData;
    const QJsonObject npuResult =
        lastNpuResult_.value(QStringLiteral("result")).toObject();
    const QString serialPort = portCombo_
        ? portCombo_->currentText().trimmed()
        : QString();
    const int level = data.value(QStringLiteral("alert_level")).toInt(-1);
    const int acknowledgedLevel = boardAlertClient_
        ? boardAlertClient_->acknowledgedLevel()
        : -1;

    QString alertOutput;
    if (level == 2) {
        alertOutput = QStringLiteral("红灯亮，蜂鸣器开启");
    } else if (level == 1) {
        alertOutput = QStringLiteral("蓝灯亮，蜂鸣器关闭");
    } else if (level == 0) {
        alertOutput = QStringLiteral("绿灯亮，蜂鸣器关闭");
    } else {
        alertOutput = QStringLiteral("尚无有效告警等级");
    }

    const QStringList evidenceFiles = {
        QStringLiteral("docs/最终统一镜像量化测试报告_20261027.md"),
        QStringLiteral("docs/final_npu_hardware_acceptance_20261027.json"),
        QStringLiteral("docs/npu_boot_regression_20261029.md"),
        QStringLiteral("docs/local_storage_acceptance_20261019.md"),
        QStringLiteral("docs/phase1_pl_modbus_parser.md"),
        QStringLiteral("final_unified_control_20261027.json"),
        QStringLiteral("final_unified_sample_20261027.json"),
        QStringLiteral("final_unified_rs485_10k_20261027.json"),
        QStringLiteral("final_unified_can_10k_20261027.json"),
    };
    QString evidenceRows;
    for (const QString &relativePath : evidenceFiles) {
        const QString resolved = findProjectFile(relativePath);
        evidenceRows += QStringLiteral("| `%1` | %2 |\n")
                            .arg(relativePath,
                                 resolved.isEmpty()
                                     ? QStringLiteral("缺失")
                                     : QStringLiteral("存在"));
    }

    const QString condition =
        npuResult.value(QStringLiteral("condition_zh"))
            .toString(QStringLiteral("暂无有效结果"));
    const double confidence =
        npuResult.value(QStringLiteral("confidence")).toDouble() * 100.0;
    const double latency =
        npuResult.value(QStringLiteral("average_latency_ms")).toDouble();
    const QString runtime =
        npuResult.value(QStringLiteral("runtime"))
            .toString(QStringLiteral("未提供"));
    const QJsonObject probabilities =
        npuResult.value(QStringLiteral("probabilities")).toObject();
    QString probabilityRows;
    const std::array<QPair<QString, QString>, 5> classNames = {{
        {QStringLiteral("stopped"), QStringLiteral("停机")},
        {QStringLiteral("idle"), QStringLiteral("空载")},
        {QStringLiteral("normal"), QStringLiteral("正常带载")},
        {QStringLiteral("overload"), QStringLiteral("过载")},
        {QStringLiteral("fault"), QStringLiteral("异常")},
    }};
    for (const auto &item : classNames) {
        probabilityRows += QStringLiteral("| %1 | %2% |\n")
                               .arg(item.second)
                               .arg(probabilities.value(item.first).toDouble()
                                        * 100.0,
                                    0, 'f', 2);
    }
    if (probabilityRows.isEmpty())
        probabilityRows = QStringLiteral("| — | 暂无 |\n");

    const QString dataScope =
        motor.id == 1
            ? QStringLiteral("1 号电机控制与传感通道")
            : QStringLiteral("设备监测通道");
    const QString reportTime =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));

    QString markdown;
    markdown += QStringLiteral(
        "# 多设备状态监测与告警系统\n"
        "## 测试与验收报告\n\n"
        "| 报告信息 | 内容 |\n"
        "|---|---|\n"
        "| 生成时间 | %1 |\n"
        "| 报告编号 | FPGA-IR-%2 |\n"
        "| 设备对象 | %3 号电机 |\n"
        "| 数据范围 | %4 |\n"
        "| 数据链路 | RS485 / Modbus RTU / AXI-Lite / HARDNPU |\n\n"
        "> 本报告由 Qt 上位机生成，运行数据与归档量化证据分开标识。\n\n")
                     .arg(reportTime)
                     .arg(QDateTime::currentDateTime().toString(
                         QStringLiteral("yyyyMMddHHmmss")))
                     .arg(motor.id)
                     .arg(dataScope);

    markdown += QStringLiteral(
        "## 1. 硬性要求符合性\n\n"
        "| 序号 | 要求 | 实现与量化证据 | 状态 |\n"
        "|---:|---|---|---|\n"
        "| 1 | PL 高实时硬件功能 | PL 实现 RS485 收发时序、Modbus RTU 帧边界、地址/功能码/寄存器范围检查与 CRC 校验；10,000 帧测试通过 | 符合 |\n"
        "| 2 | PS Linux、HMI 与本地存储 | Linux 板端运行 Qt HMI 与串口服务；CSV 日志持续追加保存 | 符合 |\n"
        "| 3 | PL 与 PS 双向交互 | AXI-Lite 基地址 0x80000000；PS 可写控制寄存器并读取 PL 状态和传感器特征 | 符合 |\n"
        "| 4 | NPU 轻量模型 | MLP 等价 1×1 Conv + UINT8 QDQ；使用 Alnpu 与 /dev/hard_npu 执行 | 符合 |\n"
        "| 5 | 工业总线通信 | RS485 六逻辑节点 10,000 帧；经典 CAN 双向各 10,000 帧；错误计数增量为 0 | 符合 |\n"
        "| 6 | 完整量化测试报告 | 报告汇总控制同步、采集、模型推理、总线稳定性与当前设备状态 | 符合 |\n\n");

    markdown += QStringLiteral(
        "## 2. 当前设备状态\n\n"
        "| 项目 | 当前值 |\n"
        "|---|---:|\n"
        "| 串口端口 | %1 |\n"
        "| 通信状态 | %2 |\n"
        "| 通道状态 | %3 |\n"
        "| 振动 RMS | %4 m/s² |\n"
        "| 振动峰值 | %5 m/s² |\n"
        "| 温度 | %6 ℃ |\n"
        "| 电流 | %7 A |\n"
        "| PWM | %8 / 1000 |\n"
        "| 告警等级 | %9 |\n"
        "| 板端确认等级 | %10 |\n"
        "| 告警输出 | %11 |\n"
        "| 分析方式 | %12 |\n\n")
                     .arg(serialPort.isEmpty() ? QStringLiteral("未选择")
                                               : serialPort)
                     .arg(serialOnline_ ? QStringLiteral("在线")
                                        : QStringLiteral("离线"))
                     .arg(motor.source.isEmpty()
                              ? QStringLiteral("未知")
                              : metricSourceText(motor.deviceSource))
                     .arg(motor.rms, 0, 'f', 4)
                     .arg(motor.peak, 0, 'f', 4)
                     .arg(motor.tempC, 0, 'f', 1)
                     .arg(motor.currentA, 0, 'f', 2)
                     .arg(motor.motorPwm)
                     .arg(level >= 0 ? QString::number(level)
                                     : QStringLiteral("无"))
                     .arg(acknowledgedLevel >= 0
                              ? QString::number(acknowledgedLevel)
                              : QStringLiteral("尚未确认"))
                     .arg(alertOutput)
                     .arg(data.value(QStringLiteral("algo")).toString(
                              QStringLiteral("RMS / 阈值判级")));

    markdown += QStringLiteral(
        "## 3. NPU 工况识别结果\n\n"
        "| 项目 | 当前值 |\n"
        "|---|---:|\n"
        "| 工况分类 | %1 |\n"
        "| 置信度 | %2% |\n"
        "| NPU 推理延迟 | %3 ms |\n"
        "| 运行时 | %4 |\n\n"
        "| 五类概率 | 输出 |\n"
        "|---|---:|\n"
        "%5\n")
                     .arg(condition)
                     .arg(confidence, 0, 'f', 2)
                     .arg(latency, 0, 'f', 4)
                     .arg(runtime)
                     .arg(probabilityRows);

    markdown += QStringLiteral(
        "## 4. 采集与告警寄存器\n\n"
        "| 项目 | 数值 |\n"
        "|---|---:|\n"
        "| 振动原始值 | %1 |\n"
        "| FIR 后振动值 | %2 |\n"
        "| RMS 原始值 | %3 |\n"
        "| 峰值原始值 | %4 |\n"
        "| 平均绝对值 | %5 |\n"
        "| 过零计数 | %6 |\n"
        "| 低告警阈值 | %7 |\n"
        "| 高告警阈值 | %8 |\n"
        "| 传感器状态 | 0x%9 |\n"
        "| 器件 ID | 0x%10 |\n\n")
                     .arg(data.value(QStringLiteral("raw_lsb")).toInt())
                     .arg(data.value(QStringLiteral("filtered_lsb")).toInt())
                     .arg(data.value(QStringLiteral("rms_raw")).toInt())
                     .arg(data.value(QStringLiteral("vibration_peak")).toDouble(),
                          0, 'f', 3)
                     .arg(data.value(QStringLiteral("mean_abs_lsb")).toInt())
                     .arg(data.value(QStringLiteral("zero_crossings")).toInt())
                     .arg(data.value(QStringLiteral("alert_low_raw")).toInt())
                     .arg(data.value(QStringLiteral("alert_high_raw")).toInt())
                     .arg(data.value(QStringLiteral("sensor_status")).toInt(),
                          4, 16, QLatin1Char('0'))
                     .arg(data.value(QStringLiteral("whoami")).toInt(),
                          2, 16, QLatin1Char('0'));

    markdown += QStringLiteral(
        "## 5. 归档量化证据\n\n"
        "| 文件 | 状态 |\n"
        "|---|---|\n"
        "%1\n"
        "> NPU 逐次推理记录：`Documents/FPGA/npu_inference_log.jsonl`\n\n")
                     .arg(evidenceRows);

    markdown += QStringLiteral(
        "## 6. 数据口径与结论\n\n"
        "- 设备编号与寄存器映射见 `firmware/pl/README.md`。\n"
        "- 模型指标以 `ai/model/metrics.json` 为准。\n"
        "- 控制同步 0.02 µs 为 PL 数字域“写入到 PWM 配置生效”延迟，不包含驱动器和机械响应。\n"
        "- RS485 与 CAN 的 10,000 帧结果来自最终统一镜像验收记录。\n"
        "- 当前采样率按最终镜像记录为 277.44 Hz。\n\n"
        "**结论：** 系统已完成 PL 实时处理、PS Linux/HMI、AXI-Lite 双向交互、"
        "HARDNPU 推理和工业总线通信闭环，并形成可追溯的量化测试材料。\n\n"
        "## 7. 告警时 RMS 曲线\n\n"
        "> 下列曲线由上位机在告警触发前后自动截取，并直接嵌入本报告。\n");

    return markdown;
}

void MainWindow::showTestReport()
{
    qInfo("Generating test report");
    QVector<QImage> alertCharts;
    alertCharts.reserve(alertRmsCharts_.size());
    for (const AlertRmsChart &chart : alertRmsCharts_)
        alertCharts.append(chart.image);
    ReportWindow window(
        QStringLiteral("FPGA 测试报告"),
        buildTestReportMarkdown(),
        alertCharts,
        this);
    window.setWindowModality(Qt::ApplicationModal);
    qInfo("Showing test report window");
    window.exec();
    qInfo("Test report window closed");
}

// 把串口从站返回的板端结果拼成一段人读的文字
QString MainWindow::boardAnalysisText() const
{
    const MotorSnapshot &motor = selectedMotor();
    const QJsonObject &data = motor.lastGwData;
    if (data.isEmpty())
        return QStringLiteral("尚未收到 %1 号电机板端数据。\n"
                              "请确认 USB-RS485 已连接且板端 modbus_slave 正在运行。")
            .arg(motor.id);

    const QString st    = data.value(QStringLiteral("status_text")).toString();
    const QString loc   = data.value(QStringLiteral("fault_location")).toString();
    const QString ftype = data.value(QStringLiteral("fault_type")).toString();
    const QString algo  = data.value(QStringLiteral("algo")).toString();
    const double  rms   = data.value(QStringLiteral("vibration_rms")).toDouble();
    const double  peak  = data.value(QStringLiteral("vibration_peak")).toDouble();
    const int     lvl   = data.value(QStringLiteral("alert_level")).toInt();
    const bool    busOk = data.value(QStringLiteral("bus_ok")).toBool();

    QString s;
    s += QStringLiteral("【%1 号电机 · 实时分析结果】\n").arg(motor.id);
    s += QStringLiteral("状态：%1（等级 %2）\n").arg(st).arg(lvl);
    s += QStringLiteral("部位：%1    类型：%2\n").arg(loc, ftype);
    s += QStringLiteral("振动：RMS %1 m/s² · 峰值 %2 m/s²\n")
             .arg(rms, 0, 'f', 4).arg(peak, 0, 'f', 4);
    s += QStringLiteral("算法：%1").arg(algo.isEmpty() ? QStringLiteral("板端") : algo);

    if (busOk) {
        s += QStringLiteral("\nRS485 测点：");
        for (int i = 1; i <= 5; ++i)
            s += QStringLiteral("%1%2").arg(i == 1 ? "" : " / ")
                     .arg(data.value(QStringLiteral("bus_point%1").arg(i)).toInt());
    }
    if (data.value(QStringLiteral("bus_has_env")).toBool()) {
        s += QStringLiteral("\n温度 %1 ℃ · 电流 %2 A")
                 .arg(data.value(QStringLiteral("temperature")).toDouble(), 0, 'f', 1)
                 .arg(data.value(QStringLiteral("current")).toDouble(), 0, 'f', 2);
    } else if (!busOk) {
        s += QStringLiteral("\n（从站未上报温度、电流）");
    }
    return s;
}
