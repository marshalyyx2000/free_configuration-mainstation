#ifndef IED_TREE_WIDGET_H
#define IED_TREE_WIDGET_H

#include "scd/scd_model.h"

#include <QWidget>

class QLineEdit;
class QTreeWidget;

class IedTreeWidget : public QWidget {
    Q_OBJECT
public:
    explicit IedTreeWidget(QWidget* parent = nullptr);
    void setDevices(const QVector<IedDeviceEntry>& devices);
    void setCommState(const QHash<QString, bool>& online);
    QString currentIedName() const;
    void selectIed(const QString& iedName);

signals:
    void iedSelected(const IedDeviceEntry& device);

private slots:
    void onSearchChanged(const QString& text);
    void onItemSelectionChanged();

private:
    void rebuildTree();

    QLineEdit* m_search = nullptr;
    QTreeWidget* m_tree = nullptr;
    QVector<IedDeviceEntry> m_devices;
    QHash<QString, bool> m_comm;
    QString m_filter;
};

#endif
