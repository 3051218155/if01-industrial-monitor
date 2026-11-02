#include "aiclient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

AiClient::AiClient(QObject *parent)
    : QObject(parent)
    , modelName_(QStringLiteral("industrial-assistant"))
{
}

void AiClient::configure(const QString &baseUrl, const QString &apiKey, const QString &modelName)
{
    baseUrl_ = baseUrl.trimmed();
    apiKey_ = apiKey.trimmed();
    if (!modelName.trimmed().isEmpty())
        modelName_ = modelName.trimmed();
}

QString AiClient::buildPrompt(const DeviceContext &ctx) const
{
    return QStringLiteral(
               "你是工业设备运维助手。请用简洁中文给出检查建议（3~5句）。\n"
               "设备号：%1\n振动RMS：%2 m/s²\n温度：%3 ℃\n电流：%4 A\n健康度：%5%\n"
               "运行状态：%6\n高亮部位：%7\n本地诊断：%8")
        .arg(ctx.deviceId)
        .arg(ctx.vibrationRms, 0, 'f', 3)
        .arg(ctx.tempC, 0, 'f', 1)
        .arg(ctx.currentA, 0, 'f', 2)
        .arg(ctx.healthPercent)
        .arg(ctx.runState)
        .arg(ctx.faultSite)
        .arg(ctx.faultLabel);
}

QString AiClient::parseResponseBody(const QByteArray &body) const
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject())
        return QString::fromUtf8(body).trimmed();

    const QJsonObject root = doc.object();

    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (!choices.isEmpty()) {
        const QJsonObject msg = choices.at(0).toObject().value(QStringLiteral("message")).toObject();
        const QString content = msg.value(QStringLiteral("content")).toString().trimmed();
        if (!content.isEmpty())
            return content;
    }

    for (const char *key : {"reply", "answer", "content", "message", "result"}) {
        const QString v = root.value(QString::fromLatin1(key)).toString().trimmed();
        if (!v.isEmpty())
            return v;
    }

    return QString::fromUtf8(body).trimmed();
}

void AiClient::requestDiagnosis(const DeviceContext &ctx)
{
    if (baseUrl_.isEmpty()) {
        emit diagnosisReady(
            QStringLiteral("未配置在线分析服务，已返回本地检查建议。\n"
                           "请关注振动 RMS 与温度变化趋势；若 RMS 持续升高且健康度下降，"
                           "优先检查轴承润滑与联轴器对中。"));
        return;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("model"), modelName_);

    QJsonArray messages;
    QJsonObject userMsg;
    userMsg.insert(QStringLiteral("role"), QStringLiteral("user"));
    userMsg.insert(QStringLiteral("content"), buildPrompt(ctx));
    messages.append(userMsg);
    payload.insert(QStringLiteral("messages"), messages);

    QJsonObject data;
    data.insert(QStringLiteral("device_id"), ctx.deviceId);
    data.insert(QStringLiteral("vibration_rms"), ctx.vibrationRms);
    data.insert(QStringLiteral("temp_c"), ctx.tempC);
    data.insert(QStringLiteral("current_a"), ctx.currentA);
    data.insert(QStringLiteral("health_percent"), ctx.healthPercent);
    data.insert(QStringLiteral("run_state"), ctx.runState);
    data.insert(QStringLiteral("fault_site"), ctx.faultSite);
    data.insert(QStringLiteral("fault_label"), ctx.faultLabel);
    payload.insert(QStringLiteral("device_context"), data);

    QNetworkRequest req{QUrl(baseUrl_)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!apiKey_.isEmpty())
        req.setRawHeader("Authorization", QByteArray("Bearer ") + apiKey_.toUtf8());

    auto *nam = new QNetworkAccessManager(this);
    QNetworkReply *reply = nam->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));

    connect(reply, &QNetworkReply::finished, this, [this, reply, nam]() {
        if (reply->error() != QNetworkReply::NoError) {
            emit requestFailed(reply->errorString());
            reply->deleteLater();
            nam->deleteLater();
            return;
        }

        const QString text = parseResponseBody(reply->readAll());
        if (text.isEmpty())
            emit requestFailed(QStringLiteral("分析服务返回为空"));
        else
            emit diagnosisReady(text);

        reply->deleteLater();
        nam->deleteLater();
    });
}
