#include "reportwindow.h"

#include <QBrush>
#include <QBuffer>
#include <QColor>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextImageFormat>
#include <QTextStream>
#include <QStringConverter>
#include <QUrl>
#include <QVBoxLayout>

ReportWindow::ReportWindow(
    const QString &title,
    const QString &markdown,
    const QVector<QImage> &alertCharts,
    QWidget *parent)
    : QDialog(parent)
    , markdown_(markdown)
    , alertCharts_(alertCharts)
    , browser_(new QTextBrowser(this))
    , statusLabel_(new QLabel(this))
{
    setWindowTitle(title);
    resize(980, 760);
    setMinimumSize(760, 560);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    QFont font(QStringLiteral("Microsoft YaHei UI"));
    font.setPointSize(10);
    browser_->setFont(font);
    browser_->setOpenExternalLinks(true);
    browser_->setMarkdown(markdown_);
    appendAlertCharts();
    layout->addWidget(browser_, 1);
    statusLabel_->setWordWrap(true);
    statusLabel_->setStyleSheet(QStringLiteral("color:#52606d;"));
    layout->addWidget(statusLabel_);

    auto *actions = new QHBoxLayout();
    auto *saveButton = new QPushButton(QStringLiteral("保存 HTML 报告"), this);
    auto *closeButton = new QPushButton(QStringLiteral("关闭"), this);
    saveButton->setMinimumWidth(130);
    closeButton->setMinimumWidth(90);
    actions->addWidget(saveButton);
    actions->addStretch();
    actions->addWidget(closeButton);
    layout->addLayout(actions);

    connect(saveButton, &QPushButton::clicked,
            this, &ReportWindow::saveHtml);
    connect(closeButton, &QPushButton::clicked,
            this, &QDialog::accept);
}

void ReportWindow::appendAlertCharts()
{
    if (alertCharts_.isEmpty())
        return;

    QTextCursor cursor(browser_->document());
    cursor.movePosition(QTextCursor::End);

    QTextBlockFormat captionFormat;
    captionFormat.setTopMargin(8);
    captionFormat.setBottomMargin(3);
    QTextCharFormat caption;
    caption.setForeground(QBrush(QColor(0x52, 0x60, 0x6D)));
    caption.setFontPointSize(10);

    for (int i = 0; i < alertCharts_.size(); ++i) {
        const QImage &image = alertCharts_.at(i);
        if (image.isNull())
            continue;

        const QString url =
            QStringLiteral("alert-rms-chart-%1").arg(i);
        browser_->document()->addResource(
            QTextDocument::ImageResource,
            QUrl(url),
            QVariant::fromValue(image));
        alertImageUrls_.append(url);

        cursor.insertBlock(captionFormat);
        cursor.insertText(
            QStringLiteral("告警 RMS 曲线 %1").arg(i + 1),
            caption);
        cursor.insertBlock();

        QTextImageFormat imageFormat;
        imageFormat.setName(url);
        const int displayWidth = 880;
        imageFormat.setWidth(displayWidth);
        imageFormat.setHeight(
            displayWidth * image.height() / image.width());
        cursor.insertImage(imageFormat);
    }
}

void ReportWindow::saveHtml()
{
    QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("保存测试报告 HTML"),
        QStringLiteral("FPGA_测试报告_%1.html")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))),
        QStringLiteral("HTML (*.html)"));
    if (path.isEmpty())
        return;

    QString html = browser_->document()->toHtml();
    for (int i = 0; i < alertImageUrls_.size() && i < alertCharts_.size(); ++i) {
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        if (!alertCharts_.at(i).save(&buffer, "PNG"))
            continue;
        const QString source =
            QStringLiteral("src=\"%1\"").arg(alertImageUrls_.at(i));
        const QString embedded =
            QStringLiteral("src=\"data:image/png;base64,%1\"")
                .arg(QString::fromLatin1(png.toBase64()));
        html.replace(source, embedded);
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        statusLabel_->setText(
            QStringLiteral("保存失败：%1").arg(file.errorString()));
        return;
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << html;
    file.close();
    statusLabel_->setText(
        QStringLiteral("报告已保存：%1").arg(path));
}
