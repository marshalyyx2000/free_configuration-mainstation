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
};

#endif
