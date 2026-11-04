#ifndef STATION_ZIP_WRITER_H
#define STATION_ZIP_WRITER_H

#include <QString>

bool writeDeflatedZipSingleFile(const QString& zipPath, const QString& arcName, const QString& filePath);

#endif
