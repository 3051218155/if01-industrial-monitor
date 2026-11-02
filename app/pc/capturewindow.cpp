#include "capturewindow.h"
#include "waveformwidget.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkProxy>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <cmath>

CaptureWindow::CaptureWindow(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("振动数据采集"));
    resize(760, 720);
    setMinimumSize(560, 620);
    auto *layout = new QVBoxLayout(this);
    auto *title = new QLabel(QStringLiteral("1号电机 · MPU6050 X轴原始振动"), this);
    title->setStyleSheet(QStringLiteral("font-size:18px; font-weight:600"));
    layout->addWidget(title);
    auto *form = new QFormLayout;
    host_ = new QLineEdit(qEnvironmentVariable("FPGA_GW_HOST", "192.168.137.100"), this);
    host_->setObjectName("captureHost");
    form->addRow(QStringLiteral("网关地址（TCP 9000）"), host_);
    condition_ = new QComboBox(this);
    condition_->setObjectName("captureCondition");
    condition_->addItem(QStringLiteral("正常运行"), "normal");
    condition_->addItem(QStringLiteral("停机背景"), "stopped");
    condition_->addItem(QStringLiteral("异常待确认"), "unknown_anomaly");
    condition_->addItem(QStringLiteral("已确认不平衡"), "imbalance");
    condition_->addItem(QStringLiteral("已确认松动"), "looseness");
    condition_->addItem(QStringLiteral("已确认不对中"), "misalignment");
    condition_->addItem(QStringLiteral("已确认轴承故障"), "bearing_fault");
    form->addRow(QStringLiteral("人工工况标签"), condition_);
    batch_ = new QLineEdit(this);
    batch_->setObjectName("captureBatch");
    form->addRow(QStringLiteral("独立实验批次"), batch_);
    position_ = new QLineEdit(this);
    position_->setObjectName("capturePosition");
    form->addRow(QStringLiteral("传感器位置 / 固定方式"), position_);
    rpm_ = new QSpinBox(this);
    rpm_->setRange(0, 100000);
    rpm_->setSpecialValueText(QStringLiteral("未测量"));
    rpm_->setSuffix(" rpm");
    form->addRow(QStringLiteral("转速"), rpm_);
    duration_ = new QSpinBox(this);
    duration_->setObjectName("captureDuration");
    duration_->setRange(5, 600);
    duration_->setValue(30);
    duration_->setSuffix(QStringLiteral(" 秒"));
    form->addRow(QStringLiteral("采集时长"), duration_);
    notes_ = new QLineEdit(this);
    notes_->setObjectName("captureNotes");
    form->addRow(QStringLiteral("负载 / 工况依据 / 备注"), notes_);
    auto *pathRow = new QHBoxLayout;
    directory_ = new QLineEdit(QDir::current().absoluteFilePath("data/captures"), this);
    directory_->setObjectName("captureDirectory");
    browse_ = new QPushButton(QStringLiteral("选择目录"), this);
    pathRow->addWidget(directory_, 1);
    pathRow->addWidget(browse_);
    form->addRow(QStringLiteral("保存目录"), pathRow);
    layout->addLayout(form);
    quality_ = new QLabel(QStringLiteral("分段寄存器采集 · 名义500Hz · 连续性未验证"), this);
    quality_->setWordWrap(true);
    quality_->setStyleSheet(QStringLiteral("color:#a36b00"));
    layout->addWidget(quality_);
    wave_ = new WaveformWidget(this);
    wave_->setTitle(QStringLiteral("X轴振动"));
    wave_->setUnit(QStringLiteral("m/s²"));
    wave_->setMinimumHeight(100);
    layout->addWidget(wave_, 1);
    progress_ = new QProgressBar(this);
    progress_->setRange(0, 100);
    layout->addWidget(progress_);
    counts_ = new QLabel(QStringLiteral("0 段 · 0 点"), this);
    layout->addWidget(counts_);
    status_ = new QLabel(QStringLiteral("待采集"), this);
    status_->setObjectName("captureStatus");
    status_->setWordWrap(true);
    layout->addWidget(status_);
    auto *buttons = new QHBoxLayout;
    auto *open = new QPushButton(QStringLiteral("打开数据目录"), this);
    start_ = new QPushButton(QStringLiteral("开始采集"), this);
    start_->setObjectName("captureStart");
    stop_ = new QPushButton(QStringLiteral("停止并保存"), this);
    stop_->setObjectName("captureStop");
    stop_->setEnabled(false);
    buttons->addWidget(open);
    buttons->addStretch();
    buttons->addWidget(start_);
    buttons->addWidget(stop_);
    layout->addLayout(buttons);
    socket_ = new QTcpSocket(this);
    socket_->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    timeout_ = new QTimer(this);
    timeout_->setSingleShot(true);
    progressTimer_ = new QTimer(this);
    progressTimer_->setInterval(200);
    connect(browse_, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("保存目录"), directory_->text());
        if (!dir.isEmpty()) directory_->setText(dir);
    });
    connect(open, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(
            sessionDirectory_.isEmpty() ? directory_->text() : sessionDirectory_));
    });
    connect(start_, &QPushButton::clicked, this, &CaptureWindow::startCapture);
    connect(stop_, &QPushButton::clicked, this, [this] { finish("user_stopped"); });
    connect(timeout_, &QTimer::timeout, this, [this] { finish("gateway_timeout"); });
    connect(progressTimer_, &QTimer::timeout, this, &CaptureWindow::updateProgress);
    connect(socket_, &QTcpSocket::connected, this, [this] {
        const QJsonObject req = verifying_
            ? QJsonObject{{"cmd", "query_device"}, {"device_id", 1}}
            : QJsonObject{{"cmd", "waveform"}, {"device_id", 1}, {"n", 256}};
        socket_->write(QJsonDocument(req).toJson(QJsonDocument::Compact) + '\n');
    });
    connect(socket_, &QTcpSocket::readyRead, this, &CaptureWindow::receive);
    connect(socket_, &QTcpSocket::errorOccurred, this, [this] {
        if (active_) finish("network_error: " + socket_->errorString());
    });
    connect(socket_, &QTcpSocket::disconnected, this, [this] {
        if (active_ && timeout_->isActive()) finish("incomplete_response");
    });
}

void CaptureWindow::startCapture()
{
    if (active_) return;
    if (host_->text().trimmed().isEmpty() || batch_->text().trimmed().isEmpty()
        || position_->text().trimmed().isEmpty() || directory_->text().trimmed().isEmpty()) {
        status_->setText(QStringLiteral("请填写网关、实验批次、安装位置和保存目录。"));
        return;
    }
    if (condition_->currentIndex() >= 3 && notes_->text().trimmed().isEmpty()) {
        status_->setText(QStringLiteral("已确认故障需要填写工况依据；未确认请选择“异常待确认”。"));
        return;
    }
    const QString id = QDateTime::currentDateTimeUtc().toString("yyyyMMddTHHmmsszzz")
        + "_" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    sessionDirectory_ = QDir(directory_->text()).absoluteFilePath(id);
    if (!QDir().mkpath(sessionDirectory_)) {
        status_->setText(QStringLiteral("无法创建保存目录。"));
        return;
    }
    csv_.setFileName(QDir(sessionDirectory_).filePath("samples.csv"));
    if (!csv_.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        status_->setText(QStringLiteral("无法创建CSV：") + csv_.errorString());
        return;
    }
    const QByteArray header = "block,index_in_block,raw_lsb,acceleration_ms2,nominal_offset_us,host_request_elapsed_ms,host_receive_elapsed_ms\n";
    if (csv_.write(header) != header.size() || !csv_.flush()) {
        csv_.close();
        status_->setText(QStringLiteral("无法写入CSV表头。"));
        return;
    }
    blocks_ = 0;
    samples_ = 0;
    wave_->clear();
    progress_->setValue(0);
    metadata_ = QJsonObject{
        {"schema_version", 1}, {"session_id", id},
        {"started_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"gateway_host", host_->text().trimmed()}, {"device_id", 1},
        {"experiment_batch", batch_->text().trimmed()},
        {"condition", condition_->currentData().toString()},
        {"condition_label", condition_->currentText()},
        {"label_origin", "operator_not_gateway_prediction"},
        {"sensor_position", position_->text().trimmed()},
        {"rpm_measured", rpm_->value() > 0 ? QJsonValue(rpm_->value()) : QJsonValue()},
        {"notes", notes_->text()}, {"requested_duration_s", duration_->value()},
        {"capture_mode", "latest_register_blocks"}, {"axis", "x"},
        {"nominal_period_us", 2000}, {"lsb_per_g", 16384}, {"g_ms2", 9.80665},
        {"continuous_sampling_verified", false}, {"hardware_sample_timestamps", false},
        {"quality", "exploratory_not_lossless_training_ready"}
    };
    elapsed_.start();
    if (!saveMetadata("verifying_source")) {
        csv_.close();
        status_->setText(QStringLiteral("无法保存元数据，采集未开始。"));
        return;
    }
    active_ = true;
    for (auto *field : {host_, directory_, batch_, position_, notes_}) field->setEnabled(false);
    condition_->setEnabled(false);
    rpm_->setEnabled(false);
    duration_->setEnabled(false);
    browse_->setEnabled(false);
    start_->setEnabled(false);
    stop_->setEnabled(true);
    emit captureActiveChanged(true);
    status_->setText(QStringLiteral("正在核验1号传感器来源…"));
    progressTimer_->start();
    request(true);
}

void CaptureWindow::request(bool verification)
{
    if (!active_) return;
    verifying_ = verification;
    buffer_.clear();
    requestMs_ = elapsed_.elapsed();
    timeout_->start(8000);
    socket_->connectToHost(host_->text().trimmed(), 9000);
}

void CaptureWindow::receive()
{
    if (!active_) return;
    buffer_ += socket_->readAll();
    if (buffer_.size() > 131072) { finish("response_too_large"); return; }
    const int end = buffer_.indexOf('\n');
    if (end < 0) return;
    timeout_->stop();
    socket_->abort();
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(buffer_.left(end), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        finish("invalid_json"); return;
    }
    const auto root = doc.object();
    if (!root.contains("code") || root.value("code").toInt(-1) != 0) {
        finish("gateway_rejected_or_unverified_protocol"); return;
    }
    const auto data = root.value("data").toObject();
    if (data.value("device_id").toInt(-1) != 1) { finish("wrong_device"); return; }
    if (verifying_) {
        const QString source = data.value("source").toString();
        if (!source.contains("MPU6050") || !source.contains(QStringLiteral("硬件"))
            || source.contains(QStringLiteral("假网关"))) {
            finish("unverified_sensor_source"); return;
        }
        metadata_.insert("gateway_snapshot", data);
        metadata_.insert("source", source);
        metadata_.insert("source_verification", "gateway_declared_not_hardware_attested");
        if (!saveMetadata("capturing")) { finish("metadata_write_error"); return; }
        status_->setText(QStringLiteral("采集中 · 1号真实传感器 · 分段数据"));
    } else {
        const auto array = data.value("samples").toArray();
        if (array.size() != 256 || data.value("period_us").toInt() != 2000) {
            finish("invalid_waveform_shape"); return;
        }
        const qint64 received = elapsed_.elapsed();
        QByteArray rows;
        for (int i = 0; i < array.size(); ++i) {
            const double raw = array[i].toDouble(1e100);
            if (!array[i].isDouble() || !std::isfinite(raw) || raw < -32768
                || raw > 32767 || raw != std::floor(raw)) {
                finish("invalid_sample"); return;
            }
            rows += QByteArray::number(blocks_) + ',' + QByteArray::number(i) + ','
                + QByteArray::number(int(raw)) + ','
                + QByteArray::number(raw / 16384.0 * 9.80665, 'f', 9) + ','
                + QByteArray::number(i * 2000) + ',' + QByteArray::number(requestMs_) + ','
                + QByteArray::number(received) + '\n';
        }
        if (csv_.write(rows) != rows.size() || !csv_.flush()) {
            finish("csv_write_error"); return;
        }
        ++blocks_;
        samples_ += array.size();
        for (const auto &sample : array)
            wave_->appendSample(sample.toDouble() / 16384.0 * 9.80665);
    }
    updateProgress();
    if (elapsed_.elapsed() >= duration_->value() * 1000LL) {
        finish("completed"); return;
    }
    QTimer::singleShot(0, this, [this] { if (active_) request(false); });
}

void CaptureWindow::updateProgress()
{
    if (!active_) return;
    progress_->setValue(qMin(100LL, elapsed_.elapsed() * 100 / (duration_->value() * 1000LL)));
    counts_->setText(QStringLiteral("%1 段 · %2 点 · %3 秒")
        .arg(blocks_).arg(samples_).arg(elapsed_.elapsed() / 1000.0, 0, 'f', 1));
}

bool CaptureWindow::saveMetadata(const QString &status)
{
    metadata_.insert("status", status);
    metadata_.insert("blocks", blocks_);
    metadata_.insert("samples", samples_);
    QSaveFile file(QDir(sessionDirectory_).filePath("metadata.json"));
    if (!file.open(QIODevice::WriteOnly)) return false;
    const auto bytes = QJsonDocument(metadata_).toJson(QJsonDocument::Indented);
    return file.write(bytes) == bytes.size() && file.commit();
}

void CaptureWindow::finish(const QString &reason)
{
    if (!active_) return;
    updateProgress();
    active_ = false;
    timeout_->stop();
    progressTimer_->stop();
    socket_->abort();
    const bool flushed = csv_.flush();
    csv_.close();
    metadata_.insert("ended_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    metadata_.insert("host_elapsed_ms", elapsed_.elapsed());
    const QString finalReason = flushed ? reason : "csv_flush_error";
    const bool saved = saveMetadata(finalReason);
    for (auto *field : {host_, directory_, batch_, position_, notes_}) field->setEnabled(true);
    condition_->setEnabled(true);
    rpm_->setEnabled(true);
    duration_->setEnabled(true);
    browse_->setEnabled(true);
    start_->setEnabled(true);
    stop_->setEnabled(false);
    emit captureActiveChanged(false);
    const QString outcome = finalReason == "completed" ? QStringLiteral("采集完成")
        : finalReason == "user_stopped" ? QStringLiteral("已停止并保存")
        : QStringLiteral("采集中断：") + finalReason;
    status_->setText(outcome + (saved ? QStringLiteral("\n") + sessionDirectory_
                                    : QStringLiteral("\n元数据保存失败，请检查磁盘。")));
}

void CaptureWindow::closeEvent(QCloseEvent *event)
{
    if (active_) finish("user_stopped");
    QDialog::closeEvent(event);
}
