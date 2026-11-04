#include "ui/ied_tree_widget.h"
#include "util/pinyin_search.h"

#include <QBrush>
#include <QHash>

#include <QHBoxLayout>
#include <QLineEdit>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

IedTreeWidget::IedTreeWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("搜索装置（拼音首字母）"));
    layout->addWidget(m_search);
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    layout->addWidget(m_tree, 1);
    connect(m_search, &QLineEdit::textChanged, this, &IedTreeWidget::onSearchChanged);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &IedTreeWidget::onItemSelectionChanged);
}

void IedTreeWidget::setDevices(const QVector<IedDeviceEntry>& devices)
{
    m_devices = devices;
    rebuildTree();
}

void IedTreeWidget::setCommState(const QHash<QString, bool>& online)
{
    m_comm = online;
    rebuildTree();
}

QString IedTreeWidget::currentIedName() const
{
    const QTreeWidgetItem* item = m_tree->currentItem();
    if (!item)
        return {};
    return item->data(0, Qt::UserRole).toString();
}

void IedTreeWidget::selectIed(const QString& iedName)
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_tree->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toString() == iedName) {
            m_tree->setCurrentItem(item);
            return;
        }
    }
}

void IedTreeWidget::onSearchChanged(const QString& text)
{
    m_filter = text;
    rebuildTree();
}

void IedTreeWidget::onItemSelectionChanged()
{
    const QTreeWidgetItem* item = m_tree->currentItem();
    if (!item)
        return;
    const int idx = item->data(0, Qt::UserRole + 1).toInt();
    if (idx >= 0 && idx < m_devices.size())
        emit iedSelected(m_devices[idx]);
}

void IedTreeWidget::rebuildTree()
{
    const QString sel = currentIedName();
    m_tree->clear();
    for (int i = 0; i < m_devices.size(); ++i) {
        const IedDeviceEntry& d = m_devices[i];
        const QString labelText = QStringLiteral("%1-[%2]%3").arg(d.no).arg(d.iedName, d.desc);
        if (!matchesMultiSegmentSearch(labelText, m_filter))
            continue;
        auto* item = new QTreeWidgetItem(m_tree);
        const bool online = m_comm.value(d.iedName, true);
        item->setText(0, labelText);
        item->setForeground(0, online ? QBrush(Qt::black) : QBrush(Qt::red));
        item->setData(0, Qt::UserRole, d.iedName);
        item->setData(0, Qt::UserRole + 1, i);
    }
    if (!sel.isEmpty())
        selectIed(sel);
}
