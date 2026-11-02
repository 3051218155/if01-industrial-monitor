#pragma once

#include "aiclient.h"

#include <array>
#include <QByteArray>
#include <QImage>
#include <QJsonObject>
#include <QMainWindow>
#include <QVector>
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QThread;
class QTimer;
class BoardAlertClient;
class GatewayWaveformClient;
class MotorGlWidget;
class MotorView3D;
class SerialModbusWorker;
class WaveformWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

public slots:
    void showTestReport();

private slots:
    void onWaveTimerTick();
    void onAiAnalyzeClicked();
    void onRemoteShow(const QJsonObject &payload);
    void onSerialPortOpened(const QString &port);
    void onSerialRegisters(
        int slave,
        const QVector<quint16> &values,
        const QString &port);
    void onFallbackRegisters(
        int slave,
        const QVector<quint16> &values,
        const QString &port);
    void onFallbackError(int slave, const QString &error);
    void onSerialError(int slave, const QString &error);
    void onSerialControlFinished(bool ok, const QString &message);
    void pollSerial();
    void refreshSerialPorts();
    void openSerialPort();
    void openFallbackPort();
    void closeFallbackPort();
    void requestNpuInference();
    void startSelectedMotor();
    void stopSelectedMotor();

signals:
    void openSerialPortRequested(const QString &port, int baud, int slave);
    void pollSerialRequested(int slave);
    void openFallbackPortRequested(const QString &port, int baud, int slave);
    void pollFallbackRequested(int slave);
    void closeFallbackPortRequested();
    void startMotorRequested(int slave, quint16 pwm);
    void stopMotorRequested(int slave);
    void writeRegistersRequested(
        int slave,
        const QVector<quint16> &registers,
        const QVector<quint16> &values);

private:
    struct MotorSnapshot {
        int id = 1;
        bool hasData = false;
        bool hasEnv = false;
        bool healthHasRms = false;
        bool healthHasTemp = false;
        bool healthHasCurrent = false;
        bool npuDeployed = false;
        bool npuModelReady = false;
        bool motorEnabled = false;
        double rms = 0.0;
        double peak = 0.0;
        double tempC = 0.0;
        double currentA = 0.0;
        int motorPwm = 0;
        int motorDirection = 0;
        int health = 96;
        QString source;
        QString deviceSource = QStringLiteral("unknown");
        QString vibrationSource = QStringLiteral("unknown");
        QString temperatureSource = QStringLiteral("unknown");
        QString currentSource = QStringLiteral("unknown");
        QString algo;
        QString state = QStringLiteral("normal");
        QString faultSite = QStringLiteral("none");
        QString faultLabel = QStringLiteral("等待串口数据");
        QJsonObject lastGwData;
    };

    struct RmsHistoryPoint {
        qint64 timestampMs = 0;
        double rmsMs2 = 0.0;
        int alertLevel = 0;
    };

    struct AlertRmsChart {
        int deviceId = 1;
        int level = 0;
        qint64 timestampMs = 0;
        QImage image;
    };

    void applyRemoteShow(const QJsonObject &payload);
    QJsonObject handleApiRequest(
        const QString &method,
        const QString &path,
        const QJsonObject &payload);
    QJsonObject apiState() const;
    QJsonObject executeApiCommand(const QJsonObject &payload);
    QString buildTestReportMarkdown() const;
    void applyDeviceData(const QJsonObject &data);
    QVector<quint16> mergedSerialRegisters(
        int slave,
        bool *fallbackUsed,
        quint16 *fallbackMask = nullptr) const;
    void processSerialRegisters(
        int slave,
        const QVector<quint16> &values,
        const QString &port,
        bool fallbackUsed,
        quint16 fallbackMask);
    void showSerialError(const QString &error);
    void refreshPanel();
    QString boardAnalysisText() const;
    void pushWaveformSamples();
    void updateHudMetrics();
    AiClient::DeviceContext currentDeviceContext() const;
    void applyViewportTwinVisual();
    void updateHealthScore(MotorSnapshot &motor);
    void selectMotor(int deviceId);
    void updateMotorButtons();
    void updateControlAvailability();
    void refreshSimulatedDevices();
    void resetWaveforms();
    MotorSnapshot &motorById(int deviceId);
    const MotorSnapshot &motorById(int deviceId) const;
    MotorSnapshot &selectedMotor();
    const MotorSnapshot &selectedMotor() const;
    void appendNpuInferenceLog(
        const QJsonObject &result,
        const QString &source);
    void recordRmsAndScheduleAlertSnapshot(
        int deviceId,
        const QJsonObject &data);
    void captureAlertRmsChart(int deviceId, int level, qint64 alertMs);

    QLabel *topTitleLabel_;
    QLabel *gatewayStatusLabel_;
    QLabel *clockLabel_;

    QLabel *brandLabel_;
    QLabel *deviceTitleLabel_;
    QLabel *rmsValueLabel_;
    QLabel *tempValueLabel_;
    QLabel *currentValueLabel_;
    QLabel *healthValueLabel_;
    QLabel *stateValueLabel_;
    QLabel *siteValueLabel_;
    QLabel *faultLabel_;
    QLabel *metaLabel_;

    QLabel *plStatusLabel_;
    QLabel *npuStatusLabel_;
    QLabel *npuConditionLabel_;
    QLabel *npuConfidenceLabel_;
    QLabel *npuRuntimeLabel_;
    QLabel *sampleRateLabel_;

    QLabel *aiResultLabel_;

    QButtonGroup *motorGroup_;
    std::array<QPushButton *, 6> motorButtons_;

    WaveformWidget *vibWave_;
    WaveformWidget *tempTrendWave_;

    QWidget *viewport_;
    bool viewportUsesJichu3D_ = false;

    QPushButton *aiAnalyzeButton_;
    QPushButton *reportButton_;
    QComboBox *portCombo_;
    QComboBox *fallbackPortCombo_;
    QCheckBox *fallbackEnable_;
    QSpinBox *pwmSpin_;
    QPushButton *startMotorButton_;
    QPushButton *stopMotorButton_;
    QTimer *waveTimer_;
    BoardAlertClient *boardAlertClient_;
    GatewayWaveformClient *gatewayWaveformClient_;
    SerialModbusWorker *serialWorker_;
    QThread *serialThread_;
    SerialModbusWorker *fallbackWorker_;
    QThread *fallbackThread_;
    QTimer *serialTimer_;
    bool serialOnline_;
    bool serialUseRealData_;
    int serialPollOutstanding_ = 0;
    bool refreshAnalysisOnNextSample_;
    int serialTick_;
    std::array<QVector<quint16>, 6> primaryRegistersByDevice_;
    std::array<QVector<quint16>, 6> fallbackRegistersByDevice_;
    QString primaryPort_;
    QString fallbackPort_;
    std::array<qint64, 6> primaryRegistersMsByDevice_{};
    std::array<qint64, 6> fallbackRegistersMsByDevice_{};
    bool fallbackOnline_;
    bool npuRequestInFlight_;
    QVector<QVector<double>> liveNpuSamples_;
    double lastLiveNpuTemperature_;
    QJsonObject lastNpuResult_;
    QByteArray lastNpuLogSignature_;
    std::array<QVector<RmsHistoryPoint>, 6> rmsHistoryByDevice_;
    std::array<int, 6> lastObservedAlertLevel_{};
    std::array<bool, 6> alertSnapshotPending_{};
    std::array<QJsonObject, 6> alertSnapshotContext_;
    QVector<AlertRmsChart> alertRmsCharts_;

    int selectedMotorIndex_;
    std::array<MotorSnapshot, 6> motors_;
    qint64 remoteHoldUntilMs_;
    int simTick_;
    int hudCounter_;
    double wavePhase_;
    int simNpuMs_;
};
