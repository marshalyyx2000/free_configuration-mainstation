#ifndef APP_PATHS_H
#define APP_PATHS_H

#include <QString>

namespace AppPaths {

QString applicationRoot();
QString writableDataRoot();
QString masterSimRoot();
QString outDir();
QString iedListPath();
QString defaultSmcdPath();
QString defaultScdPath();

} // namespace AppPaths

#endif
