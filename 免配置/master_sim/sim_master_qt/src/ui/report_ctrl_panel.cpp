#include "ui/report_ctrl_panel.h"

#include "mms/mms_client_bridge.h"
#include "event/event_format.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QBrush>
#include <QColor>
#include <QSet>
#include <QMessageBox>
#include <QProgressDialog>
#include <QShowEvent>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

QString ReportCtrlPanel::jsonValueText(const QJsonValue& v)
{
    if (v.isBool())
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (v.isDouble())
        return QString::number(v.toDouble(), 'g', 12);
    if (v.isString())
        return v.toString();
    if (v.isArray())
        return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    if (v.isObject())
        return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    return QString();
}

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

    auto* lowerWrap = new QWidget(split);
    auto* lowerLayout = new QVBoxLayout(lowerWrap);
    lowerLayout->setContentsMargins(0, 0, 0, 0);
    lowerLayout->setSpacing(4);

    auto* midBtns = new QHBoxLayout;
    m_btnReadDs = new QPushButton(tr("召唤数据集值"), lowerWrap);
    m_btnSubCurrent = new QPushButton(tr("订阅当前数据集"), lowerWrap);
    m_btnUnsubCurrent = new QPushButton(tr("取消订阅当前数据集"), lowerWrap);
    m_btnGiCurrent = new QPushButton(tr("总召唤当前数据集"), lowerWrap);
    midBtns->addWidget(m_btnReadDs);
    midBtns->addWidget(m_btnSubCurrent);
    midBtns->addWidget(m_btnUnsubCurrent);
    midBtns->addWidget(m_btnGiCurrent);
    midBtns->addStretch();

    m_lower = new QTableWidget(lowerWrap);
    m_lower->setColumnCount(4);
    m_lower->setHorizontalHeaderLabels({ tr("描述"), tr("地址"), tr("前值"), tr("后值") });
    m_lower->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_lower->horizontalHeader()->setStretchLastSection(true);

    lowerLayout->addLayout(midBtns);
    lowerLayout->addWidget(m_lower, 1);

    split->addWidget(m_upper);
    split->addWidget(lowerWrap);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 4);

    auto* btns = new QHBoxLayout;
    m_btnAll = new QPushButton(tr("全部订阅"), this);
    m_btnReadAll = new QPushButton(tr("读当前装置所有数据集"), this);
    btns->addWidget(m_btnAll);
    btns->addWidget(m_btnReadAll);
    btns->addStretch();

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(split, 1);
    layout->addLayout(btns);

    connect(m_upper, &QTableWidget::itemSelectionChanged, this, &ReportCtrlPanel::onUpperSelectionChanged);
    connect(m_btnReadDs, &QPushButton::clicked, this, &ReportCtrlPanel::onReadCurrentDataset);
    connect(m_btnSubCurrent, &QPushButton::clicked, this, &ReportCtrlPanel::onSubscribeCurrent);
    connect(m_btnUnsubCurrent, &QPushButton::clicked, this, &ReportCtrlPanel::onUnsubscribeCurrent);
    connect(m_btnGiCurrent, &QPushButton::clicked, this, &ReportCtrlPanel::onGiCurrent);
    connect(m_btnAll, &QPushButton::clicked, this, &ReportCtrlPanel::onSubscribeAll);
    connect(m_btnReadAll, &QPushButton::clicked, this, &ReportCtrlPanel::onReadAllDeviceDatasets);

    if (type == ReportPanelType::Wave) {
        m_upper->setColumnCount(2);
        m_upper->setHorizontalHeaderLabels({ tr("文件名"), tr("大小") });
        m_btnReadDs->setVisible(false);
        m_btnSubCurrent->setVisible(false);
        m_btnUnsubCurrent->setVisible(false);
        m_btnGiCurrent->setVisible(false);
        m_btnAll->setVisible(false);
        m_btnReadAll->setVisible(false);
    } else if (type == ReportPanelType::ReportInst) {
        m_btnReadDs->setVisible(false);
        m_btnSubCurrent->setVisible(false);
        m_btnUnsubCurrent->setVisible(false);
        m_btnGiCurrent->setVisible(false);
        m_btnAll->setVisible(false);
    }
    updateCurrentDatasetButtons();
}

ReportCtrlPanel::~ReportCtrlPanel()
{
    finishBulkRead();
}

void ReportCtrlPanel::setIed(const IedDeviceEntry& dev, const IedModel* model)
{
    m_dev = dev;
    m_model = model;
    m_waveListDone = false;
    rebuildUpper();
}

void ReportCtrlPanel::setSubscribedHandlers(const QSet<QString>& handlers,
    const QHash<QString, QString>& failures)
{
    m_subscribed = handlers;
    m_subscribeFail = failures;
    for (int r = 0; r < m_rows.size(); ++r) {
        const QString& href = m_rows[r].handlerRef;
        QString status;
        QColor color;
        if (m_subscribed.contains(href)) {
            status = tr("已订阅");
            color = QColor(QStringLiteral("#080"));
        } else if (m_subscribeFail.contains(href)) {
            status = tr("订阅失败");
            color = QColor(QStringLiteral("#c00"));
        }
        QTableWidgetItem* item = m_upper->item(r, 6);
        if (!item && m_upper->columnCount() > 6)
            item = new QTableWidgetItem;
        if (item) {
            item->setText(status);
            item->setForeground(QBrush(color));
            if (!m_upper->item(r, 6))
                m_upper->setItem(r, 6, item);
        }
    }
    updateCurrentDatasetButtons();
}

void ReportCtrlPanel::rebuildUpper()
{
    m_rows.clear();
    m_upper->setRowCount(0);
    m_lower->setRowCount(0);
    if (m_type == ReportPanelType::Wave) {
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
        row.dsRef = ScdModelLoader::datasetRef(m_dev.iedName, rpt.datSet, rpt.ldInst);
        row.rcbName = rpt.name;
        row.buffered = rpt.buffered;
        row.ldInst = rpt.ldInst;
        row.handlerRef = ScdModelLoader::rcbHandlerRef(m_dev.iedName, rpt.name, rpt.buffered, rpt.ldInst);
        row.getRef = ScdModelLoader::rcbGetRef(m_dev.iedName, rpt.name, ScdModelLoader::upstreamRptInst(), rpt.buffered,
            rpt.ldInst);
        m_rows.append(row);

        const DataSetEntry* ds = ScdModelLoader::findDataset(m_model, rpt.datSet, rpt.ldInst);
        const int memberCount = ds ? ds->members.size() : 0;

        const int r = m_upper->rowCount();
        m_upper->insertRow(r);
        m_upper->setItem(r, 0, new QTableWidgetItem(ds && !ds->desc.isEmpty() ? ds->desc : rpt.datSet));
        m_upper->setItem(r, 1, new QTableWidgetItem(memberCount > 0 ? QString::number(memberCount) : QString()));
        m_upper->setItem(r, 2, new QTableWidgetItem(rpt.datSet));
        m_upper->setItem(r, 3, new QTableWidgetItem(rpt.name));
        const QString rptId = (rpt.rptId.isEmpty() || rpt.rptId == QStringLiteral("NULL")) ? QString() : rpt.rptId;
        m_upper->setItem(r, 4, new QTableWidgetItem(rptId));
        m_upper->setItem(r, 5, new QTableWidgetItem(rpt.confRev > 0 ? QString::number(rpt.confRev) : QString()));
        QTableWidgetItem* subItem = new QTableWidgetItem;
        if (m_subscribed.contains(row.handlerRef)) {
            subItem->setText(tr("已订阅"));
            subItem->setForeground(QBrush(QColor(QStringLiteral("#080"))));
        } else if (m_subscribeFail.contains(row.handlerRef)) {
            subItem->setText(tr("订阅失败"));
            subItem->setForeground(QBrush(QColor(QStringLiteral("#c00"))));
        }
        m_upper->setItem(r, 6, subItem);
    }
    if (m_upper->rowCount() > 0 && m_upper->currentRow() < 0)
        m_upper->selectRow(0);
    if (m_upper->currentRow() >= 0)
        populateLowerFromModel(currentRow());
    updateCurrentDatasetButtons();
}

void ReportCtrlPanel::populateLowerFromModel(const RowInfo& row)
{
    if (m_type == ReportPanelType::Wave)
        return;
    const DataSetEntry* ds = ScdModelLoader::findDataset(m_model, row.dsName, row.ldInst);
    if (!ds || ds->members.isEmpty()) {
        m_lower->setRowCount(0);
        return;
    }
    m_lower->setRowCount(ds->members.size());
    for (int i = 0; i < ds->members.size(); ++i) {
        const DataSetMember& m = ds->members[i];
        m_lower->setItem(i, 0, new QTableWidgetItem(m.desc));
        m_lower->setItem(i, 1, new QTableWidgetItem(m.address));
        m_lower->setItem(i, 2, new QTableWidgetItem(QString()));
        m_lower->setItem(i, 3, new QTableWidgetItem(QString()));
    }
    applyCachedValues(row);
}

void ReportCtrlPanel::applyCachedValues(const RowInfo& row)
{
    if (row.dsRef.isEmpty() || !m_dsCache.contains(row.dsRef))
        return;
    fillLowerFromJson(m_dsCache.value(row.dsRef));
}

void ReportCtrlPanel::updateEntryCountForRef(const QString& dsRef, int count)
{
    for (int r = 0; r < m_rows.size(); ++r) {
        if (m_rows[r].dsRef != dsRef)
            continue;
        if (QTableWidgetItem* item = m_upper->item(r, 1))
            item->setText(count > 0 ? QString::number(count) : QString());
    }
}

void ReportCtrlPanel::onConnectionReady()
{
    auto* tabs = qobject_cast<QTabWidget*>(parentWidget());
    if (tabs && tabs->currentWidget() != this)
        return;
    if (m_upper->currentRow() < 0 && m_upper->rowCount() > 0)
        m_upper->selectRow(0);
}

void ReportCtrlPanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_type != ReportPanelType::Wave || m_waveListDone || !m_mms || !m_mms->connected())
        return;
    m_waveListDone = true;
    m_mms->listFiles(m_dev.wavePath, QString());
}

ReportCtrlPanel::RowInfo ReportCtrlPanel::currentRow() const
{
    const int r = m_upper->currentRow();
    if (r < 0 || r >= m_rows.size())
        return {};
    return m_rows[r];
}

const ReportCtrlPanel::RowInfo* ReportCtrlPanel::rowForReport(const QString& rcb, const QString& dsRef) const
{
    if (!dsRef.isEmpty()) {
        for (const RowInfo& row : m_rows) {
            if (ScdModelLoader::datasetRefMatches(dsRef, row.dsRef))
                return &row;
        }
    }
    if (!rcb.isEmpty()) {
        for (const RowInfo& row : m_rows) {
            if (ScdModelLoader::rcbReportRefMatches(rcb, row.handlerRef, row.getRef))
                return &row;
        }
    }
    return nullptr;
}

void ReportCtrlPanel::ingestReportItems(const RowInfo& row, const QJsonArray& items)
{
    const DataSetEntry* ds = ScdModelLoader::findDataset(m_model, row.dsName, row.ldInst);
    QJsonObject cache = m_dsCache.value(row.dsRef);
    QJsonArray rows = cache.value(QStringLiteral("rows")).toArray();
    const int memberCount = ds ? ds->members.size() : rows.size();
    if (rows.size() < memberCount) {
        for (int i = rows.size(); i < memberCount; ++i) {
            QJsonObject o;
            o.insert(QStringLiteral("idx"), i);
            if (ds)
                o.insert(QStringLiteral("name"), ds->members[i].address);
            rows.append(o);
        }
    }
    auto matchRow = [&](const QString& name, int idx) -> int {
        if (!name.isEmpty() && ds) {
            for (int i = 0; i < ds->members.size(); ++i) {
                if (ScdModelLoader::addressMatchesMmsDataRef(ds->members[i].address, name))
                    return i;
            }
        }
        if (!name.isEmpty()) {
            for (int i = 0; i < rows.size(); ++i) {
                const QString existing = rows.at(i).toObject().value(QStringLiteral("name")).toString();
                if (existing == name || ScdModelLoader::addressMatchesMmsDataRef(existing, name))
                    return i;
            }
        }
        return idx;
    };
    for (const QJsonValue& v : items) {
        const QJsonObject it = v.toObject();
        const QString name = it.value(QStringLiteral("name")).toString();
        const int idx = it.value(QStringLiteral("idx")).toInt(-1);
        const int r = matchRow(name, idx);
        if (r < 0)
            continue;
        while (rows.size() <= r) {
            QJsonObject o;
            o.insert(QStringLiteral("idx"), rows.size());
            rows.append(o);
        }
        QJsonObject o = rows.at(r).toObject();
        o.insert(QStringLiteral("idx"), r);
        if (!name.isEmpty())
            o.insert(QStringLiteral("name"), name);
        else if (ds && r < ds->members.size() && o.value(QStringLiteral("name")).toString().isEmpty())
            o.insert(QStringLiteral("name"), ds->members[r].address);
        o.insert(QStringLiteral("value"), it.value(QStringLiteral("value")));
        rows[r] = o;
    }
    cache.insert(QStringLiteral("ref"), row.dsRef);
    cache.insert(QStringLiteral("rows"), rows);
    m_dsCache.insert(row.dsRef, cache);
    updateEntryCountForRef(row.dsRef, rows.size());
}

void ReportCtrlPanel::onUpperSelectionChanged()
{
    populateLowerFromModel(currentRow());
    updateCurrentDatasetButtons();
}

void ReportCtrlPanel::updateCurrentDatasetButtons()
{
    if (!m_btnSubCurrent || !m_btnUnsubCurrent)
        return;
    if (m_type == ReportPanelType::Wave || m_type == ReportPanelType::ReportInst)
        return;
    const RowInfo row = currentRow();
    const bool subbed = !row.handlerRef.isEmpty() && m_subscribed.contains(row.handlerRef);
    const QString hi = QStringLiteral(
        "QPushButton { background-color: #1b7a3d; color: #ffffff; font-weight: bold; }");
    m_btnSubCurrent->setStyleSheet(subbed ? QString() : hi);
    m_btnUnsubCurrent->setStyleSheet(subbed ? hi : QString());
    m_btnSubCurrent->setDefault(!subbed);
    m_btnUnsubCurrent->setDefault(subbed);
}

void ReportCtrlPanel::onReadCurrentDataset()
{
    const RowInfo row = currentRow();
    if (row.dsRef.isEmpty() || !m_mms || !m_mms->connected()) {
        QMessageBox::warning(this, tr("提示"), tr("请先连接 SubStation 并选择数据集"));
        return;
    }
    m_pendingDsReq = row.dsRef;
    emit logLine(tr("召唤数据集 %1").arg(row.dsRef));
    emit deviceEvent(StationEventCategory::datasetRead(), row.dsRef, tr("召唤数据集"));
    m_mms->readDataSet(row.dsRef, row.dsRef);
}

void ReportCtrlPanel::onSubscribeCurrent()
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

void ReportCtrlPanel::onUnsubscribeCurrent()
{
    const RowInfo row = currentRow();
    if (row.handlerRef.isEmpty() || !m_mms)
        return;
    if (!m_subscribed.contains(row.handlerRef)) {
        emit logLine(tr("取消订阅未执行：当前数据集未订阅"));
        return;
    }
    emit logLine(tr("取消订阅当前数据集 %1").arg(row.handlerRef));
    m_mms->disableRcb(row.handlerRef, row.getRef);
}

void ReportCtrlPanel::onGiCurrent()
{
    const RowInfo row = currentRow();
    if (row.handlerRef.isEmpty() || !m_mms)
        return;
    if (!m_subscribed.contains(row.handlerRef)) {
        emit logLine(tr("总召唤未执行：请先订阅当前数据集"));
        return;
    }
    emit logLine(tr("发送总召唤 %1").arg(row.handlerRef));
    m_mms->triggerGi(row.handlerRef);
}

void ReportCtrlPanel::onSubscribeAll()
{
    emit subscribeAllForIed(m_dev.iedName);
}

bool ReportCtrlPanel::ownsDataSetRef(const QString& dsRef) const
{
    if (dsRef.isEmpty())
        return false;
    if (dsRef == m_pendingDsReq)
        return true;
    for (const RowInfo& row : m_rows) {
        if (row.dsRef == dsRef)
            return true;
    }
    return false;
}

QStringList ReportCtrlPanel::allDeviceDataSetRefs() const
{
    QStringList refs;
    if (!m_model)
        return refs;
    QSet<QString> seen;
    for (const DataSetEntry& ds : m_model->datasets) {
        const QString ref = ScdModelLoader::datasetRef(m_dev.iedName, ds.name, ds.ldInst);
        if (ref.isEmpty() || seen.contains(ref))
            continue;
        seen.insert(ref);
        refs.append(ref);
    }
    return refs;
}

QStringList ReportCtrlPanel::mmsItemsForRef(const QString& dsRef) const
{
    QStringList items;
    if (!m_model || dsRef.isEmpty())
        return items;
    for (const DataSetEntry& ds : m_model->datasets) {
        if (ScdModelLoader::datasetRef(m_dev.iedName, ds.name, ds.ldInst) != dsRef)
            continue;
        for (const DataSetMember& m : ds.members) {
            const QString item = ScdModelLoader::mmsItemIdFromAddress(m.address);
            if (!item.isEmpty())
                items.append(item);
        }
        break;
    }
    return items;
}

void ReportCtrlPanel::onReadAllDeviceDatasets()
{
    if (!m_mms || !m_mms->connected()) {
        QMessageBox::warning(this, tr("提示"), tr("请先连接 SubStation"));
        return;
    }
    const QStringList refs = allDeviceDataSetRefs();
    if (refs.isEmpty()) {
        QMessageBox::information(this, tr("提示"), tr("当前装置没有可读取的数据集"));
        return;
    }
    if (m_bulkActive)
        finishBulkRead();

    m_bulkQueue = refs;
    m_bulkTotal = refs.size();
    m_bulkDone = 0;
    m_bulkActive = true;
    m_bulkAwaitingResponse = false;
    emit deviceEvent(StationEventCategory::datasetRead(),
        QStringLiteral("@%1").arg(m_dev.iedName),
        tr("召唤全装置数据集（%1 个）").arg(m_bulkTotal));
    m_bulkProg = new QProgressDialog(tr("正在读取 %1 的数据集…").arg(m_dev.iedName), tr("取消"), 0, m_bulkTotal, this);
    m_bulkProg->setWindowModality(Qt::WindowModal);
    m_bulkProg->setMinimumDuration(0);
    m_bulkProg->setValue(0);
    connect(m_bulkProg, &QProgressDialog::canceled, this, [this]() { finishBulkRead(); });
    sendNextBulkRead();
}

void ReportCtrlPanel::sendNextBulkRead()
{
    if (!m_bulkActive)
        return;
    if (!m_mms || !m_mms->connected()) {
        finishBulkRead();
        return;
    }
    if (m_bulkQueue.isEmpty() || (m_bulkProg && m_bulkProg->wasCanceled())) {
        finishBulkRead();
        return;
    }
    const QString ref = m_bulkQueue.takeFirst();
    m_pendingDsReq = ref;
    m_bulkAwaitingResponse = true;
    m_mms->readDataSet(ref, ref);
}

void ReportCtrlPanel::scheduleBulkNext(bool stopAfter)
{
    if (!m_bulkActive)
        return;
    QTimer::singleShot(stopAfter ? 0 : 30, this, [this, stopAfter]() {
        if (!m_bulkActive)
            return;
        if (stopAfter) {
            finishBulkRead();
            return;
        }
        sendNextBulkRead();
    });
}

void ReportCtrlPanel::onBulkStepDone(bool stopAfter)
{
    if (!m_bulkActive)
        return;
    m_bulkAwaitingResponse = false;
    ++m_bulkDone;
    if (m_bulkProg)
        m_bulkProg->setValue(qMin(m_bulkDone, m_bulkTotal));
    if (stopAfter || m_bulkDone >= m_bulkTotal) {
        finishBulkRead();
        return;
    }
    scheduleBulkNext(false);
}

void ReportCtrlPanel::finishBulkRead()
{
    const bool wasActive = m_bulkActive;
    const int done = m_bulkDone;
    const int total = m_bulkTotal;
    m_bulkActive = false;
    m_bulkAwaitingResponse = false;
    m_bulkQueue.clear();
    m_pendingDsReq.clear();
    if (wasActive && done > 0) {
        emit deviceEvent(StationEventCategory::datasetReadDone(), QStringLiteral("@%1").arg(m_dev.iedName),
            tr("全装置数据集召唤完成：%1/%2").arg(done).arg(total));
    }
    if (m_bulkProg) {
        disconnect(m_bulkProg, nullptr, this, nullptr);
        m_bulkProg->close();
        m_bulkProg->deleteLater();
        m_bulkProg = nullptr;
    }
    const RowInfo row = currentRow();
    if (!row.dsRef.isEmpty())
        applyCachedValues(row);
}

void ReportCtrlPanel::fillLowerFromJson(const QJsonObject& evt)
{
    const QJsonArray members = evt.value(QStringLiteral("rows")).toArray();
    updateEntryCountForRef(evt.value(QStringLiteral("ref")).toString(), members.size());
    if (m_lower->rowCount() < members.size())
        m_lower->setRowCount(members.size());
    for (int i = 0; i < members.size(); ++i) {
        const QJsonObject m = members[i].toObject();
        const QString name = m.value(QStringLiteral("name")).toString();
        const QString val = jsonValueText(m.value(QStringLiteral("value")));
        if (!m_lower->item(i, 0) || m_lower->item(i, 0)->text().isEmpty()) {
            const QString desc = name.section(QLatin1Char('.'), -1).section(QLatin1Char('['), 0, 0);
            m_lower->setItem(i, 0, new QTableWidgetItem(desc.isEmpty() ? name : desc));
        }
        if (!m_lower->item(i, 1) || m_lower->item(i, 1)->text().isEmpty())
            m_lower->setItem(i, 1, new QTableWidgetItem(name));
        if (m_lower->item(i, 2))
            m_lower->item(i, 2)->setText(m_lower->item(i, 3) ? m_lower->item(i, 3)->text() : val);
        else
            m_lower->setItem(i, 2, new QTableWidgetItem(val));
        if (m_lower->item(i, 3))
            m_lower->item(i, 3)->setText(val);
        else
            m_lower->setItem(i, 3, new QTableWidgetItem(val));
    }
}

void ReportCtrlPanel::handleMmsEvent(const QJsonObject& evt)
{
    const QString type = evt.value(QStringLiteral("evt")).toString();
    if (type == QStringLiteral("disconnected")) {
        m_waveListDone = false;
        if (m_bulkActive)
            finishBulkRead();
        return;
    }
    if (type == QStringLiteral("dataset")) {
        const QString ref = evt.value(QStringLiteral("ref")).toString();
        m_dsCache.insert(ref, evt);
        updateEntryCountForRef(ref, evt.value(QStringLiteral("rows")).toArray().size());
        if (m_bulkActive) {
            if (m_bulkAwaitingResponse && ref == m_pendingDsReq)
                onBulkStepDone(false);
            return;
        }
        const int rowCount = evt.value(QStringLiteral("rows")).toArray().size();
        if (ownsDataSetRef(ref) && currentRow().dsRef == ref)
            emit logLine(tr("召唤完成 %1，共 %2 条").arg(ref).arg(rowCount));
        if (ownsDataSetRef(ref))
            emit deviceEvent(StationEventCategory::datasetReadDone(), ref,
                tr("召唤完成，共 %1 条").arg(rowCount));
        if (currentRow().dsRef == ref)
            fillLowerFromJson(evt);
        return;
    }
    if (type == QStringLiteral("error") && m_bulkActive && m_bulkAwaitingResponse) {
        const QString msg = evt.value(QStringLiteral("msg")).toString();
        const bool fatal = msg == QStringLiteral("CONNECTION_LOST") || msg == QStringLiteral("NOT_CONNECTED")
            || msg == QStringLiteral("CONNECTION_REJECTED");
        onBulkStepDone(fatal);
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
    const QString rcb = evt.value(QStringLiteral("rcb")).toString();
    const QString dsRef = evt.value(QStringLiteral("ds")).toString();
    const RowInfo* owner = rowForReport(rcb, dsRef);
    if (!owner)
        return;
    ingestReportItems(*owner, evt.value(QStringLiteral("items")).toArray());
    if (currentRow().dsRef == owner->dsRef)
        fillLowerFromJson(m_dsCache.value(owner->dsRef));
}
