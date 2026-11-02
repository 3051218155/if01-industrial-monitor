#pragma once

#include <QDialog>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>

class QTextBrowser;
class QLabel;

class ReportWindow final : public QDialog
{
    Q_OBJECT

public:
    explicit ReportWindow(
        const QString &title,
        const QString &markdown,
        const QVector<QImage> &alertCharts,
        QWidget *parent = nullptr);

private slots:
    void saveHtml();

private:
    void appendAlertCharts();

    QString markdown_;
    QVector<QImage> alertCharts_;
    QStringList alertImageUrls_;
    QTextBrowser *browser_;
    QLabel *statusLabel_;
};
