#ifndef CONFIG_GENERATOR_H
#define CONFIG_GENERATOR_H

#include <QString>
#include <QVector>

struct ConnectedDeviceEntry {
    QString iedName;
    QString ip;
    QString desc;
};

class ConfigGenerator {
public:
    static QVector<ConnectedDeviceEntry> parseConnectedAps(const QString& scdPath);
    static QVector<ConnectedDeviceEntry> generate(const QString& scdPath, const QString& outDir);
    /** SubStation 的 SDDI.ComStatus 引用 SUBSTATION_SDDI_V2.00。SCD 本身没有该 LNodeType，
     *  安装目录也经常找不到 SubStation.icd，这里按 id 把缺失的类型补进 DataTypeTemplates。 */
    static QByteArray ensureSubstationSddiTypes(QByteArray smcd);
};

#endif
