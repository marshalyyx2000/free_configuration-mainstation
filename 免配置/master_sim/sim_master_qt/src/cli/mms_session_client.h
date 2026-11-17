#ifndef MMS_SESSION_CLIENT_H
#define MMS_SESSION_CLIENT_H

#include <QJsonObject>
#include <QString>

int runSessionClient(const QJsonObject& cmd, int timeoutMs = -1);
bool sessionServerAlive();

#endif
