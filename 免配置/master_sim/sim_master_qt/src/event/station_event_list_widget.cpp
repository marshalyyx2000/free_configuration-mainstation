#include "event/station_event_list_widget.h"
#include "util/pinyin_search.h"

#include <QAbstractTableModel>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QStackedWidget>
#include <QTableView>
#include <QTreeWidget>
#include <QVBoxLayout>

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
        return 8;
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
            return e.iedName;
        case 2:
            return e.rcbName;
        case 3:
            return e.pointName;
        case 4:
            return e.address;
        case 5:
            return e.oldValue;
        case 6:
            return e.newValue;
        case 7:
            return e.reason;
        default:
            return {};
        }
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
            return {};
        static const char* headers[] = { "时间", "装置", "报告控制块", "点描述", "地址", "前值", "后值", "原因" };
        if (section >= 0 && section < 8)
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
    m_treeIed->setHeaderLabels({ tr("装置 / 事件"), tr("后值") });
    connect(m_treeIed, &QTreeWidget::itemDoubleClicked, this, &StationEventListWidget::onTreeDoubleClicked);

    m_treeRcb = new QTreeWidget(this);
    m_treeRcb->setHeaderLabels({ tr("RCB / 事件"), tr("后值") });
    connect(m_treeRcb, &QTreeWidget::itemDoubleClicked, this, &StationEventListWidget::onTreeDoubleClicked);

    m_stack->addWidget(m_table);
    m_stack->addWidget(m_treeIed);
    m_stack->addWidget(m_treeRcb);
    layout->addWidget(m_stack, 1);

    connect(m_search, &QLineEdit::textChanged, this, &StationEventListWidget::onFilterChanged);
    connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &StationEventListWidget::onFilterChanged);
    if (m_store)
        connect(m_store, &EventStore::eventAppended, this, &StationEventListWidget::refreshView);
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
            auto* child = new QTreeWidgetItem(parent, { e.pointName, e.newValue });
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
            auto* child = new QTreeWidgetItem(parent, { e.pointName, e.newValue });
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
