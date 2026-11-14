#ifndef SCD_MODEL_H
#define SCD_MODEL_H

#include <QHash>
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
    /** @a uiFast 为 true 时不索引全 LD 的 DOI（大站启动快，点描述多为 FCDA/doName）。 */
    static QVector<IedModel> parseModelsFromSmcd(const QString& path, bool uiFast = true);
    bool loadIedList(const QString& path);
    bool loadSmcd(const QString& path, bool uiFast = true);
    void adoptParsedModels(QVector<IedModel> models);
    /** Drop parsed SMCD / IEDList so a large station.SMCD can be rewritten on disk. */
    void clearLoadedModels();
    const QVector<IedDeviceEntry>& devices() const { return m_devices; }
    const IedModel* modelFor(const QString& iedName) const;
    /** Longest IEDList ied_name prefix matching MMS ref (e.g. E1Q1SB41PROT/... → E1Q1SB41). */
    QString resolveIedFromMmsRef(const QString& mmsRef) const;
    /** 子站代理 MMS 模型：LD0/PROT/CTRL/MEAS/RCD。PIGO/RPIT 为 GOOSE LD，不 enable。
     *  @a skippedNonProxy 非空时写入被跳过的 ReportControl 条数。 */
    QVector<RcbDescriptor> allSubscribeRcbs(int* skippedNonProxy = nullptr) const;
    static bool isUpstreamProxyLd(const QString& iedName, const QString& ldInst);
    static bool datasetMatchesPanel(const QString& dsName, ReportPanelType type);
    static bool reportMatchesPanel(const ReportEntry& rpt, ReportPanelType type);
    static QString ldName(const QString& iedName, const QString& ldInst);
    static QString datasetRef(const QString& iedName, const QString& dsName,
        const QString& ldInst = QStringLiteral("LD0"));
    static QString rcbHandlerRef(const QString& iedName, const QString& rcbName, bool buffered,
        const QString& ldInst = QStringLiteral("LD0"));
    static QString rcbGetRef(const QString& iedName, const QString& rcbName, const QString& rptno, bool buffered,
        const QString& ldInst = QStringLiteral("LD0"));
    /** 经子站对上订阅时 RCB 实例恒为 01（与台账 rptno 仅下联有关）。 */
    static QString upstreamRptInst() { return QStringLiteral("1"); }
    static QString fcdaMmsAddress(const QString& iedName, const QDomElement& fcda);
    static QString mmsItemIdFromAddress(const QString& address);
    /** MMS report evt "rcb" (typically rcbGetRef) vs SCD handler / get references. */
    static bool rcbReportRefMatches(const QString& reportRcb, const QString& handlerRef, const QString& getRef);
    /** SCD FCDA address (LD/LN.DO[FC]) vs MMS data reference in reports. */
    static bool addressMatchesMmsDataRef(const QString& scdAddress, const QString& mmsDataRef);
    /** DatSet in reports may be prefixed with '@'. */
    static bool datasetRefMatches(const QString& a, const QString& b);
    static const ReportEntry* findReportForHandler(const IedModel* model, const QString& handlerRef);
    static const DataSetEntry* findDataset(const IedModel* model, const QString& dsName,
        const QString& ldInst = QStringLiteral("LD0"));

private:
    void rebuildModelIndex();
    QVector<IedDeviceEntry> m_devices;
    QVector<IedModel> m_models;
    QHash<QString, int> m_modelByIed;
};

#endif
