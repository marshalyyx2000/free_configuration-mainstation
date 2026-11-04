#ifndef REPORT_CTRL_PANEL_H
#define REPORT_CTRL_PANEL_H

#include "scd/scd_model.h"

#include <QJsonObject>
#include <QSet>
#include <QWidget>

class QTableWidget;
class QPushButton;
class MmsClientBridge;

class ReportCtrlPanel : public QWidget {
    Q_OBJECT
public:
    ReportCtrlPanel(ReportPanelType type, MmsClientBridge* mms, QWidget* parent = nullptr);
    void setIed(const IedDeviceEntry& dev, const IedModel* model);
    void setSubscribedHandlers(const QSet<QString>& handlers);
    void handleMmsEvent(const QJsonObject& evt);

signals:
    void subscribeAllForIed(const QString& iedName);
    void logLine(const QString& msg);

private slots:
    void onUpperSelectionChanged();
    void onSubscribe();
    void onSubscribeAll();
    void onGi();
    void onReadValues();

private:
    struct RowInfo {
        QString dsName;
        QString dsRef;
        QString rcbName;
        QString handlerRef;
        QString getRef;
        bool buffered = true;
    };

    void rebuildUpper();
    RowInfo currentRow() const;
    void fillLowerFromJson(const QJsonObject& evt);

    ReportPanelType m_type;
    MmsClientBridge* m_mms = nullptr;
    IedDeviceEntry m_dev;
    const IedModel* m_model = nullptr;
    QSet<QString> m_subscribed;

    QTableWidget* m_upper = nullptr;
    QTableWidget* m_lower = nullptr;
    QPushButton* m_btnSub = nullptr;
    QPushButton* m_btnAll = nullptr;
    QPushButton* m_btnGi = nullptr;
    QPushButton* m_btnRead = nullptr;
    QVector<RowInfo> m_rows;
    QString m_pendingDsReq;
};

#endif
