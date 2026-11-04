#include "ui/report_ctrl_panel.h"

#include "mms/mms_client_bridge.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

ReportCtrlPanel::ReportCtrlPanel(ReportPanelType type, MmsClientBridge* mms, QWidget* parent)
    : QWidget(parent)
    , m_type(type)
    , m_mms(mms)
{
    auto* split = new QSplitter(Qt::Vertical, this);
    m_upper = new QTableWidget(split);
    m_upper->setColumnCount(7);
    m_upper->setHorizontalHeaderLabels(
        { tr("数据集描述"), tr("条目"), tr("数据集"), tr("RCB"), tr("RptID"), tr("ConfRev"), tr("订阅") });
    m_upper->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_upper->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_upper->horizontalHeader()->setStretchLastSection(true);

    m_lower = new QTableWidget(split);
    m_lower->setColumnCount(4);
    m_lower->setHorizontalHeaderLabels({ tr("描述"), tr("地址"), tr("前值"), tr("后值") });
    m_lower->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_lower->horizontalHeader()->setStretchLastSection(true);

    split->addWidget(m_upper);
    split->addWidget(m_lower);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 4);

    auto* btns = new QHBoxLayout;
    m_btnSub = new QPushButton(tr("订阅 MMS 报告控制块"), this);
    m_btnGi = new QPushButton(tr("发起总召唤"), this);
    m_btnAll = new QPushButton(tr("全部订阅"), this);
    m_btnRead = new QPushButton(tr("读当前值"), this);
    btns->addWidget(m_btnSub);
    btns->addWidget(m_btnGi);
    btns->addWidget(m_btnAll);
    btns->addWidget(m_btnRead);
    btns->addStretch();

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(split, 1);
    layout->addLayout(btns);

    connect(m_upper, &QTableWidget::itemSelectionChanged, this, &ReportCtrlPanel::onUpperSelectionChanged);
    connect(m_btnSub, &QPushButton::clicked, this, &ReportCtrlPanel::onSubscribe);
    connect(m_btnAll, &QPushButton::clicked, this, &ReportCtrlPanel::onSubscribeAll);
    connect(m_btnGi, &QPushButton::clicked, this, &ReportCtrlPanel::onGi);
    connect(m_btnRead, &QPushButton::clicked, this, &ReportCtrlPanel::onReadValues);

    if (type == ReportPanelType::Wave) {
        m_upper->setColumnCount(2);
        m_upper->setHorizontalHeaderLabels({ tr("文件名"), tr("大小") });
        m_btnSub->setVisible(false);
        m_btnGi->setVisible(false);
        m_btnAll->setVisible(false);
    } else if (type == ReportPanelType::ReportInst) {
        m_btnSub->setVisible(false);
        m_btnGi->setVisible(false);
        m_btnAll->setVisible(false);
    }
}

void ReportCtrlPanel::setIed(const IedDeviceEntry& dev, const IedModel* model)
{
    m_dev = dev;
    m_model = model;
    rebuildUpper();
}

void ReportCtrlPanel::setSubscribedHandlers(const QSet<QString>& handlers)
{
    m_subscribed = handlers;
    rebuildUpper();
}

void ReportCtrlPanel::rebuildUpper()
{
    m_rows.clear();
    m_upper->setRowCount(0);
    m_lower->setRowCount(0);
    if (m_type == ReportPanelType::Wave) {
        if (m_mms && m_mms->connected())
            m_mms->listFiles(m_dev.wavePath, QString());
        return;
    }
    if (!m_model)
        return;
    QSet<QString> seenDs;
    for (const ReportEntry& rpt : m_model->reports) {
        if (!ScdModelLoader::reportMatchesPanel(rpt, m_type))
            continue;
        if (seenDs.contains(rpt.datSet))
            continue;
        seenDs.insert(rpt.datSet);
        RowInfo row;
        row.dsName = rpt.datSet;
        row.dsRef = ScdModelLoader::datasetRef(m_dev.iedName, rpt.datSet);
        row.rcbName = rpt.name;
        row.buffered = rpt.buffered;
        row.handlerRef = ScdModelLoader::rcbHandlerRef(m_dev.iedName, rpt.name, rpt.buffered);
        row.getRef = ScdModelLoader::rcbGetRef(m_dev.iedName, rpt.name, m_dev.rptno, rpt.buffered);
        m_rows.append(row);

        const int r = m_upper->rowCount();
        m_upper->insertRow(r);
        m_upper->setItem(r, 0, new QTableWidgetItem(rpt.datSet));
        m_upper->setItem(r, 1, new QTableWidgetItem(QString()));
        m_upper->setItem(r, 2, new QTableWidgetItem(rpt.datSet));
        m_upper->setItem(r, 3, new QTableWidgetItem(rpt.name));
        m_upper->setItem(r, 4, new QTableWidgetItem(QString()));
        m_upper->setItem(r, 5, new QTableWidgetItem(QString()));
        const bool sub = m_subscribed.contains(row.handlerRef);
        m_upper->setItem(r, 6, new QTableWidgetItem(sub ? tr("已订阅") : QString()));
    }
}

ReportCtrlPanel::RowInfo ReportCtrlPanel::currentRow() const
{
    const int r = m_upper->currentRow();
    if (r < 0 || r >= m_rows.size())
        return {};
    return m_rows[r];
}

void ReportCtrlPanel::onUpperSelectionChanged()
{
    onReadValues();
}

void ReportCtrlPanel::onReadValues()
{
    const RowInfo row = currentRow();
    if (!row.dsRef.isEmpty() && m_mms && m_mms->connected()) {
        m_pendingDsReq = row.dsRef;
        m_mms->readDataSet(row.dsRef, row.dsRef);
    }
}

void ReportCtrlPanel::onSubscribe()
{
    const RowInfo row = currentRow();
    if (row.handlerRef.isEmpty() || !m_mms)
        return;
    m_mms->enableRcb(QJsonObject{
        { QStringLiteral("handlerRef"), row.handlerRef },
        { QStringLiteral("rcbGetRef"), row.getRef },
        { QStringLiteral("dsRef"), row.dsRef },
    });
}

void ReportCtrlPanel::onSubscribeAll()
{
    emit subscribeAllForIed(m_dev.iedName);
}

void ReportCtrlPanel::onGi()
{
    const RowInfo row = currentRow();
    if (!row.handlerRef.isEmpty() && m_mms)
        m_mms->triggerGi(row.handlerRef);
}

void ReportCtrlPanel::fillLowerFromJson(const QJsonObject& evt)
{
    const QJsonArray members = evt.value(QStringLiteral("rows")).toArray();
    m_lower->setRowCount(members.size());
    for (int i = 0; i < members.size(); ++i) {
        const QJsonObject m = members[i].toObject();
        m_lower->setItem(i, 0, new QTableWidgetItem(m.value(QStringLiteral("name")).toString()));
        m_lower->setItem(i, 1, new QTableWidgetItem(m.value(QStringLiteral("name")).toString()));
        m_lower->setItem(i, 2, new QTableWidgetItem(m.value(QStringLiteral("value")).toString()));
        m_lower->setItem(i, 3, new QTableWidgetItem(m.value(QStringLiteral("value")).toString()));
    }
}

void ReportCtrlPanel::handleMmsEvent(const QJsonObject& evt)
{
    const QString type = evt.value(QStringLiteral("evt")).toString();
    if (type == QStringLiteral("dataset") && evt.value(QStringLiteral("ref")).toString() == m_pendingDsReq) {
        fillLowerFromJson(evt);
        return;
    }
    if (type == QStringLiteral("files") && m_type == ReportPanelType::Wave) {
        const QJsonArray files = evt.value(QStringLiteral("entries")).toArray();
        m_upper->setRowCount(files.size());
        for (int i = 0; i < files.size(); ++i) {
            const QJsonObject f = files[i].toObject();
            m_upper->setItem(i, 0, new QTableWidgetItem(f.value(QStringLiteral("name")).toString()));
            m_upper->setItem(i, 1, new QTableWidgetItem(QString::number(f.value(QStringLiteral("size")).toInt())));
        }
        return;
    }
    if (type != QStringLiteral("report"))
        return;
    const QJsonArray items = evt.value(QStringLiteral("items")).toArray();
    for (const QJsonValue& v : items) {
        const QJsonObject it = v.toObject();
        const QString name = it.value(QStringLiteral("name")).toString();
        const QString val = it.value(QStringLiteral("value")).toString();
        for (int r = 0; r < m_lower->rowCount(); ++r) {
            if (m_lower->item(r, 0) && m_lower->item(r, 0)->text() == name) {
                if (m_lower->item(r, 2))
                    m_lower->item(r, 2)->setText(m_lower->item(r, 3) ? m_lower->item(r, 3)->text() : val);
                if (m_lower->item(r, 3))
                    m_lower->item(r, 3)->setText(val);
            }
        }
    }
}
