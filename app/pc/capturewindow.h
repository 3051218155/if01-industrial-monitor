#pragma once

#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonObject>

class QComboBox;
class QLineEdit;
class QSpinBox;
class QLabel;
class QPushButton;
class QProgressBar;
class QTcpSocket;
class QTimer;
class WaveformWidget;

class CaptureWindow : public QDialog
{
    Q_OBJECT
public:
    explicit CaptureWindow(QWidget *parent = nullptr);
signals:
    void captureActiveChanged(bool active);
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    void startCapture();
    void request(bool verification);
    void receive();
    void finish(const QString &reason);
    bool saveMetadata(const QString &status);
    void updateProgress();
    QLineEdit *host_, *directory_, *batch_, *position_, *notes_;
    QComboBox *condition_;
    QSpinBox *rpm_, *duration_;
    QLabel *status_, *counts_, *quality_;
    QPushButton *start_, *stop_, *browse_;
    QProgressBar *progress_;
    WaveformWidget *wave_;
    QTcpSocket *socket_;
    QTimer *timeout_, *progressTimer_;
    QFile csv_;
    QElapsedTimer elapsed_;
    QJsonObject metadata_;
    QByteArray buffer_;
    QString sessionDirectory_;
    bool active_ = false;
    bool verifying_ = false;
    int blocks_ = 0;
    qint64 samples_ = 0;
    qint64 requestMs_ = 0;
};
