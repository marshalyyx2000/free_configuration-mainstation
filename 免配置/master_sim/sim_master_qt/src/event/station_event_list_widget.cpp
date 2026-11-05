#include "event/station_event_list_widget.h"
#include "event/event_format.h"
#include "util/pinyin_search.h"

#include <QAbstractItemModel>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QStackedWidget>
#include <QTableView>
#include <QTreeWidget>
#include <QTimer>

static QString eventDescLabel(const StationEvent& e)
{
    const QString body = !e.pointName.isEmpty() ? e.pointName : e.address;
    if (e.category.isEmpty())
        return body.isEmpty() ? QStringLiteral("未知信息点") : body;
    if (body.isEmpty())
        return e.category;
    return e.category + QStringLiteral(" · ") + body;
}

class StationEventModel : public QAbstractTableModel {
public:
    explicit StationEventModel(QObject* parent = nullptr)
        : QAbstractTableModel(parent)
    {
    }

    void setEvents(QVector<StationEvent> events)
    {
        beginResetModel();
        m_events = std::move(events);
        endResetModel();
    }

    const StationEvent* eventAt(int row) const
    {
        if (row < 0 || row >= m_events.size())
            return nullptr;
        return &m_events[row];
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        if (parent.isValid())
            return 0;
        return m_events.size();
    }

    int columnCount(const QModelIndex& parent = QModelIndex()) const override
    {
        if (parent.isValid())
            return 0;
        return 9;
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || index.row() >= m_events.size())
            return {};
        const StationEvent& e = m_events[index.row()];
        if (role != Qt::DisplayRole)
            return {};
        switch (index.column()) {
        case 0:
            return e.time.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"));
        case 1:
            return e.category;
        case 2:
            return e.iedName;
        case 3:
            return e.rcbName;
        case 4:
            return e.pointName;
        case 5:
            return e.address;
        case 6:
            return e.oldValue;
        case 7:
            return e.newValue;
        case 8:
            return e.reasonText.isEmpty() ? QString::number(e.reason) : e.reasonText;
        default:
            return {};
        }
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
            return {};
        static const char* headers[] = { "时间", "类别", "装置", "报告控制块", "事件描述", "地址", "前值", "后值", "原因" };
        if (section >= 0 && section < 9)
            return QString::fromUtf8(headers[section]);
        return {};
    }

private:
    QVector<StationEvent> m_events;
};

StationEventListWidget::StationEventListWidget(EventStore* store, QWidget* parent)
    : QWidget(parent)
    , m_store(store)
{
    auto* layout = new QVBoxLayout(this);
    auto* bar = new QHBoxLayout;
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("多段搜索（空格分隔，支持拼音首字母）"));
    m_mode = new QComboBox(this);
    m_mode->addItem(tr("平铺列表"), Flat);
    m_mode->addItem(tr("按装置级"), ByIed);
    m_mode->addItem(tr("按报告控制块级"), ByRcb);
    bar->addWidget(m_search, 1);
    bar->addWidget(m_mode);
    layout->addLayout(bar);

    m_stack = new QStackedWidget(this);
    m_table = new QTableView(this);
    m_model = new StationEventModel(this);
    m_table->setModel(m_model);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(m_table, &QTableView::doubleClicked, this, &StationEventListWidget::onTableDoubleClicked);

    m_treeIed = new QTreeWidget(this);
    m_treeIed->setHeaderLabels({ tr("事件描述"), tr("后值/摘要"), tr("类别") });
    m_treeIed->setColumnCount(3);
    connect(m_treeIed, &QTreeWidget::itemSelectionChanged, this, [this]() { updateDetailFromTree(m_treeIed); });
    connect(m_treeIed, &QTreeWidget::itemDoubleClicked, this, &StationEventListWidget::onTreeDoubleClicked);

    m_treeRcb = new QTreeWidget(this);
    m_treeRcb->setHeaderLabels({ tr("事件描述"), tr("后值/摘要"), tr("类别") });
    m_treeRcb->setColumnCount(3);
    connect(m_treeRcb, &QTreeWidget::itemSelectionChanged, this, [this]() { updateDetailFromTree(m_treeRcb); });
    connect(m_treeRcb, &QTreeWidget::itemDoubleClicked, this, &StationEventListWidget::onTreeDoubleClicked);

    m_stack->addWidget(m_table);
    m_stack->addWidget(m_treeIed);
    m_stack->addWidget(m_treeRcb);
    layout->addWidget(m_stack, 1);

    auto* detailTitle = new QLabel(tr("事件详细信息"), this);
    m_detail = new QPlainTextEdit(this);
    m_detail->setReadOnly(true);
    m_detail->setMaximumHeight(140);
    m_detail->setPlaceholderText(tr("选中一条事件以查看描述与变化详情"));
    layout->addWidget(detailTitle);
    layout->addWidget(m_detail);

    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]() {
        const QModelIndexList rows = m_table->selectionModel()->selectedRows();
        if (rows.isEmpty()) {
            m_detail->clear();
            return;
        }
        const StationEvent* ev = m_model->eventAt(rows.first().row());
        if (ev)
            m_detail->setPlainText(ev->detailText());
    });

    connect(m_search, &QLineEdit::textChanged, this, &StationEventListWidget::onFilterChanged);
    connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &StationEventListWidget::onFilterChanged);
    if (m_store)
        connect(m_store, &EventStore::eventAppended, this, [this]() {
            if (!m_refreshTimer) {
                m_refreshTimer = new QTimer(this);
                m_refreshTimer->setSingleShot(true);
                m_refreshTimer->setInterval(80);
                connect(m_refreshTimer, &QTimer::timeout, this, &StationEventListWidget::refreshView);
            }
            m_refreshTimer->start();
        });
    refreshView();
}

void StationEventListWidget::refreshView()
{
    rebuildFiltered();
}

void StationEventListWidget::onFilterChanged()
{
    rebuildFiltered();
}

void StationEventListWidget::rebuildFiltered()
{
    const QString query = m_search->text();
    const ViewMode mode = static_cast<ViewMode>(m_mode->currentData().toInt());
    m_stack->setCurrentIndex(mode);

    QVector<StationEvent> all = m_store ? m_store->snapshot() : QVector<StationEvent>();
    m_filtered.clear();
    for (int i = all.size() - 1; i >= 0; --i) {
        if (matchesMultiSegmentSearch(all[i].searchText(), query))
            m_filtered.append(all[i]);
    }

    if (mode == Flat) {
        m_model->setEvents(m_filtered);
        return;
    }

    QTreeWidget* tree = mode == ByIed ? m_treeIed : m_treeRcb;
    tree->clear();
    if (mode == ByIed) {
        QHash<QString, QTreeWidgetItem*> groups;
        for (const StationEvent& e : m_filtered) {
            QTreeWidgetItem* parent = groups.value(e.iedName);
            if (!parent) {
                parent = new QTreeWidgetItem(tree, { e.iedName, QString() });
                groups.insert(e.iedName, parent);
            }
            auto* child = new QTreeWidgetItem(parent,
                { eventDescLabel(e), e.newValue.isEmpty() ? QStringLiteral("-") : e.newValue,
                    e.category.isEmpty() ? e.reasonText : e.category });
            child->setData(0, Qt::UserRole, QVariant::fromValue(e));
        }
    } else {
        QHash<QString, QTreeWidgetItem*> groups;
        for (const StationEvent& e : m_filtered) {
            const QString key = e.iedName + QStringLiteral(" / ") + e.rcbName;
            QTreeWidgetItem* parent = groups.value(key);
            if (!parent) {
                parent = new QTreeWidgetItem(tree, { key, QString() });
                groups.insert(key, parent);
            }
            auto* child = new QTreeWidgetItem(parent,
                { eventDescLabel(e), e.newValue.isEmpty() ? QStringLiteral("-") : e.newValue,
                    e.category.isEmpty() ? e.reasonText : e.category });
            child->setData(0, Qt::UserRole, QVariant::fromValue(e));
        }
    }
    tree->expandToDepth(0);
}

void StationEventListWidget::onTableDoubleClicked(const QModelIndex& idx)
{
    const StationEvent* ev = m_model->eventAt(idx.row());
    if (ev)
        emit eventActivated(*ev);
}

void StationEventListWidget::onTreeDoubleClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column);
    if (!item || !item->parent())
        return;
    const QVariant v = item->data(0, Qt::UserRole);
    if (v.canConvert<StationEvent>())
        emit eventActivated(v.value<StationEvent>());
}

void StationEventListWidget::updateDetailFromTree(QTreeWidget* tree)
{
    if (!m_detail || !tree)
        return;
    const QList<QTreeWidgetItem*> sel = tree->selectedItems();
    if (sel.isEmpty() || !sel.first()->parent()) {
        m_detail->clear();
        return;
    }
    const QVariant v = sel.first()->data(0, Qt::UserRole);
    if (v.canConvert<StationEvent>())
        m_detail->setPlainText(v.value<StationEvent>().detailText());
}
