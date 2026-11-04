#ifndef SCD_MODEL_H
#define SCD_MODEL_H

#include <QString>
#include <QVector>

enum class ReportPanelType {
    Setting,
    RelayEna,
    Din,
    Ain,
    Other,
    ReportInst,
    Wave
};

struct IedDeviceEntry {
    int no = 0;
    QString iedName;
    QString desc;
    QString chPara;
    QString rptno = QStringLiteral("9");
    QString wavePath = QStringLiteral("/COMTRADE/");
};

struct DataSetEntry {
    QString name;
    QString desc;
};

struct ReportEntry {
    QString name;
    QString desc;
    QString datSet;
    bool buffered = true;
};

struct IedModel {
    QString iedName;
    QString desc;
    QVector<DataSetEntry> datasets;
    QVector<ReportEntry> reports;
};

struct RcbDescriptor {
    QString iedName;
    QString rcbName;
    QString handlerRef;
    QString getRef;
    QString dsRef;
    QString dsName;
    int inst = 9;
    bool buffered = true;
    int trgOps = 0x44;
    int optFlds = 0x3900;
};

class ScdModelLoader {
public:
    bool loadIedList(const QString& path);
    bool loadSmcd(const QString& path);
    const QVector<IedDeviceEntry>& devices() const { return m_devices; }
    const IedModel* modelFor(const QString& iedName) const;
    QVector<RcbDescriptor> allSubscribeRcbs() const;
    static bool datasetMatchesPanel(const QString& dsName, ReportPanelType type);
    static bool reportMatchesPanel(const ReportEntry& rpt, ReportPanelType type);
    static QString datasetRef(const QString& iedName, const QString& dsName);
    static QString rcbHandlerRef(const QString& iedName, const QString& rcbName, bool buffered);
    static QString rcbGetRef(const QString& iedName, const QString& rcbName, const QString& rptno, bool buffered);

private:
    QVector<IedDeviceEntry> m_devices;
    QVector<IedModel> m_models;
};

#endif
