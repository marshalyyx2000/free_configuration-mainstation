#ifndef STATION_EVENT_LIST_WIDGET_H
#define STATION_EVENT_LIST_WIDGET_H

#include "event/event_store.h"

#include <QTreeWidgetItem>
#include <QWidget>

class QComboBox;
class QPlainTextEdit;
class QTimer;
class QLineEdit;
class QStackedWidget;
class QTableView;
class QTreeWidget;
class StationEventModel;

class StationEventListWidget : public QWidget {
    Q_OBJECT
public:
    explicit StationEventListWidget(EventStore* store, QWidget* parent = nullptr);
    void refreshView();

signals:
    void eventActivated(const StationEvent& ev);

private slots:
    void onFilterChanged();
    void onTableDoubleClicked(const QModelIndex& idx);
    void onTreeDoubleClicked(QTreeWidgetItem* item, int column);

private:
    void rebuildFiltered();
    void updateDetailFromTree(QTreeWidget* tree);
    enum ViewMode { Flat, ByIed, ByRcb };

    EventStore* m_store = nullptr;
    QLineEdit* m_search = nullptr;
    QComboBox* m_mode = nullptr;
    QStackedWidget* m_stack = nullptr;
    QTableView* m_table = nullptr;
    QTreeWidget* m_treeIed = nullptr;
    QTreeWidget* m_treeRcb = nullptr;
    StationEventModel* m_model = nullptr;
    QVector<StationEvent> m_filtered;
    QPlainTextEdit* m_detail = nullptr;
    QTimer* m_refreshTimer = nullptr;
};

#endif
