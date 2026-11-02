#pragma once

#include <QObject>
#include <QJsonObject>

#include <functional>

class QTcpServer;

// 香橙派查询、设备选择和控制 API（端口 8760）
class ShowApiServer : public QObject
{
    Q_OBJECT

public:
    explicit ShowApiServer(QObject *parent = nullptr);
    ~ShowApiServer() override;

    bool listen(quint16 port = 8760);
    void setRequestHandler(
        std::function<QJsonObject(
            const QString &method,
            const QString &path,
            const QJsonObject &payload)> handler);

signals:
    void remoteShowRequested(const QJsonObject &payload);

private slots:
    void onNewConnection();

private:
    void handleClient(class QTcpSocket *socket);

    QTcpServer *server_ = nullptr;
    std::function<QJsonObject(
        const QString &,
        const QString &,
        const QJsonObject &)> handler_;
};
