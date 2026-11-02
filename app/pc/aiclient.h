#pragma once

#include <QObject>
#include <QString>

// 可选在线分析客户端：配置 API 地址与 Key 后启用
class AiClient : public QObject
{
    Q_OBJECT

public:
    explicit AiClient(QObject *parent = nullptr);

    // 例如 baseUrl = "https://your-api/v1/chat/completions"
    // apiKey 可为 Bearer Token；留空则返回本地检查建议
    void configure(const QString &baseUrl, const QString &apiKey, const QString &modelName);

    struct DeviceContext {
        int deviceId = 1;
        double vibrationRms = 0.0;
        double tempC = 0.0;
        double currentA = 0.0;
        int healthPercent = 100;
        QString runState;
        QString faultSite;
        QString faultLabel;
    };

public slots:
    void requestDiagnosis(const DeviceContext &ctx);

signals:
    void diagnosisReady(const QString &text);
    void requestFailed(const QString &reason);

private:
    QString buildPrompt(const DeviceContext &ctx) const;
    QString parseResponseBody(const QByteArray &body) const;

    QString baseUrl_;
    QString apiKey_;
    QString modelName_;
};
