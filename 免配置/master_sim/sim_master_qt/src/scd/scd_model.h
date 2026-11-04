#ifndef SCD_MODEL_H
#define SCD_MODEL_H

#include <QString>
#include <QVector>

class QDomElement;

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

struct DataSetMember {
    QString desc;
    QString address;
};

struct DataSetEntry {
    QString name;
    QString desc;
    QString ldInst = QStringLiteral("LD0");
    QVector<DataSetMember> members;
};

struct ReportEntry {
    QString name;
    QString desc;
    QString datSet;
    QString ldInst = QStringLiteral("LD0");
    QString rptId;
    int confRev = 0;
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
    static QString ldName(const QString& iedName, const QString& ldInst);
    static QString datasetRef(const QString& iedName, const QString& dsName,
        const QString& ldInst = QStringLiteral("LD0"));
    static QString rcbHandlerRef(const QString& iedName, const QString& rcbName, bool buffered,
        const QString& ldInst = QStringLiteral("LD0"));
    static QString rcbGetRef(const QString& iedName, const QString& rcbName, const QString& rptno, bool buffered,
        const QString& ldInst = QStringLiteral("LD0"));
    static QString fcdaMmsAddress(const QString& iedName, const QDomElement& fcda);
    static QString mmsItemIdFromAddress(const QString& address);
    static const DataSetEntry* findDataset(const IedModel* model, const QString& dsName);

private:
    QVector<IedDeviceEntry> m_devices;
    QVector<IedModel> m_models;
};

#endif
