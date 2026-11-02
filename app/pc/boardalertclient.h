#pragma once

#include <QObject>

class QTcpSocket;
class QTimer;

class BoardAlertClient final : public QObject
{
    Q_OBJECT

public:
    explicit BoardAlertClient(QObject *parent = nullptr);

    void setLevel(int level);
    int acknowledgedLevel() const { return acknowledgedLevel_; }

private:
    void ensureSend();
    void sendRequest();
    void scheduleRetry();
    void consumeResponses();

    QTcpSocket *socket_;
    QTimer *retryTimer_;
    int desiredLevel_;
    int acknowledgedLevel_;
    bool hasDesiredLevel_;
    bool requestInFlight_;
};
