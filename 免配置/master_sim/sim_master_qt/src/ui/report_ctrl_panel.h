#ifndef REPORT_CTRL_PANEL_H
#define REPORT_CTRL_PANEL_H

#include "scd/scd_model.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QWidget>

class QTableWidget;
class QPushButton;
class QProgressDialog;
class MmsClientBridge;

class ReportCtrlPanel : public QWidget {
    Q_OBJECT
public:
    explicit ReportCtrlPanel(ReportPanelType type, MmsClientBridge* mms, QWidget* parent = nullptr);
    ~ReportCtrlPanel() override;
    void setIed(const IedDeviceEntry& dev, const IedModel* model);
    void setSubscribedHandlers(const QSet<QString>& handlers);
    void handleMmsEvent(const QJsonObject& evt);
    void onConnectionReady();

protected:
    void showEvent(QShowEvent* event) override;

signals:
    void subscribeAllForIed(const QString& iedName);
    void logLine(const QString& msg);

private slots:
    void onUpperSelectionChanged();
    void onSubscribeCurrent();
    void onGiCurrent();
    void onReadCurrentDataset();
    void onSubscribeAll();
    void onReadAllDeviceDatasets();

private:
    struct RowInfo {
        QString dsName;
        QString dsRef;
        QString rcbName;
        QString handlerRef;
        QString getRef;
        QString ldInst;
        bool buffered = true;
    };

    void rebuildUpper();
    RowInfo currentRow() const;
    const RowInfo* rowForReport(const QString& rcb, const QString& dsRef) const;
    void ingestReportItems(const RowInfo& row, const QJsonArray& items);
    void populateLowerFromModel(const RowInfo& row);
    void applyCachedValues(const RowInfo& row);
    void updateEntryCountForRef(const QString& dsRef, int count);
    void fillLowerFromJson(const QJsonObject& evt);
    bool ownsDataSetRef(const QString& dsRef) const;
    QStringList allDeviceDataSetRefs() const;
    QStringList mmsItemsForRef(const QString& dsRef) const;
    void sendNextBulkRead();
    void finishBulkRead();
    void onBulkStepDone(bool stopAfter = false);
    void scheduleBulkNext(bool stopAfter = false);
    static QString jsonValueText(const QJsonValue& v);

    ReportPanelType m_type;
    MmsClientBridge* m_mms = nullptr;
    IedDeviceEntry m_dev;
    const IedModel* m_model = nullptr;
    QSet<QString> m_subscribed;
    QHash<QString, QJsonObject> m_dsCache;

    QTableWidget* m_upper = nullptr;
    QTableWidget* m_lower = nullptr;
    QPushButton* m_btnReadDs = nullptr;
    QPushButton* m_btnSubCurrent = nullptr;
    QPushButton* m_btnGiCurrent = nullptr;
    QPushButton* m_btnAll = nullptr;
    QPushButton* m_btnReadAll = nullptr;
    QVector<RowInfo> m_rows;
    QString m_pendingDsReq;

    bool m_bulkActive = false;
    bool m_bulkAwaitingResponse = false;
    bool m_waveListDone = false;
    QStringList m_bulkQueue;
    int m_bulkTotal = 0;
    int m_bulkDone = 0;
    QProgressDialog* m_bulkProg = nullptr;
};

#endif
