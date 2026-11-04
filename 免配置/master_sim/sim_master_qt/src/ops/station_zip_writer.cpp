#include "ops/station_zip_writer.h"

#include <QFile>
#include <QtEndian>

extern "C" {
#include "miniz.h"
}

static QByteArray rawDeflate(const QByteArray& raw)
{
    if (raw.isEmpty())
        return QByteArray();

    mz_stream stream {};
    const int init = mz_deflateInit2(&stream, MZ_DEFAULT_COMPRESSION, MZ_DEFLATED, -MZ_DEFAULT_WINDOW_BITS, 9,
        MZ_DEFAULT_STRATEGY);
    if (init != MZ_OK)
        return {};

    const mz_ulong bound = mz_deflateBound(&stream, static_cast<mz_ulong>(raw.size()));
    QByteArray compressed(static_cast<int>(bound), Qt::Uninitialized);

    stream.next_in = reinterpret_cast<unsigned char*>(const_cast<char*>(raw.constData()));
    stream.avail_in = static_cast<unsigned int>(raw.size());
    stream.next_out = reinterpret_cast<unsigned char*>(compressed.data());
    stream.avail_out = static_cast<unsigned int>(bound);

    const int deflateRc = mz_deflate(&stream, MZ_FINISH);
    mz_deflateEnd(&stream);
    if (deflateRc != MZ_STREAM_END)
        return {};

    compressed.resize(static_cast<int>(stream.total_out));
    return compressed;
}

bool writeDeflatedZipSingleFile(const QString& zipPath, const QString& arcName, const QString& filePath)
{
    QFile in(filePath);
    if (!in.open(QIODevice::ReadOnly))
        return false;
    const QByteArray raw = in.readAll();
    in.close();

    const QByteArray compressed = rawDeflate(raw);
    if (compressed.isEmpty() && !raw.isEmpty())
        return false;

    const quint32 crc = mz_crc32(0, reinterpret_cast<const unsigned char*>(raw.constData()), raw.size());
    const QByteArray name = arcName.toUtf8();

    QFile out(zipPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;

    const quint32 localSig = qToLittleEndian(quint32(0x04034b50));
    const quint16 ver = qToLittleEndian(quint16(20));
    const quint16 flags = qToLittleEndian(quint16(0));
    const quint16 method = qToLittleEndian(quint16(8));
    const quint16 modTime = qToLittleEndian(quint16(0));
    const quint16 modDate = qToLittleEndian(quint16(0));
    const quint32 crcLe = qToLittleEndian(crc);
    const quint32 compLe = qToLittleEndian(static_cast<quint32>(compressed.size()));
    const quint32 uncompLe = qToLittleEndian(static_cast<quint32>(raw.size()));
    const quint16 nameLen = qToLittleEndian(static_cast<quint16>(name.size()));
    const quint16 extraLen = qToLittleEndian(quint16(0));

    const qint64 localOffset = out.pos();
    out.write(reinterpret_cast<const char*>(&localSig), 4);
    out.write(reinterpret_cast<const char*>(&ver), 2);
    out.write(reinterpret_cast<const char*>(&flags), 2);
    out.write(reinterpret_cast<const char*>(&method), 2);
    out.write(reinterpret_cast<const char*>(&modTime), 2);
    out.write(reinterpret_cast<const char*>(&modDate), 2);
    out.write(reinterpret_cast<const char*>(&crcLe), 4);
    out.write(reinterpret_cast<const char*>(&compLe), 4);
    out.write(reinterpret_cast<const char*>(&uncompLe), 4);
    out.write(reinterpret_cast<const char*>(&nameLen), 2);
    out.write(reinterpret_cast<const char*>(&extraLen), 2);
    out.write(name);
    out.write(compressed);

    const qint64 centralOffset = out.pos();
    const quint32 centSig = qToLittleEndian(quint32(0x02014b50));
    const quint16 verMade = qToLittleEndian(quint16(20));
    const quint16 verNeed = qToLittleEndian(quint16(20));
    const quint16 centFlags = qToLittleEndian(quint16(0));
    const quint16 centMethod = qToLittleEndian(quint16(8));
    const quint16 centModTime = qToLittleEndian(quint16(0));
    const quint16 centModDate = qToLittleEndian(quint16(0));
    const quint32 centCrc = crcLe;
    const quint32 centComp = compLe;
    const quint32 centUncomp = uncompLe;
    const quint16 centNameLen = nameLen;
    const quint16 centExtraLen = extraLen;
    const quint16 commentLen = qToLittleEndian(quint16(0));
    const quint16 diskStart = qToLittleEndian(quint16(0));
    const quint16 intAttr = qToLittleEndian(quint16(0));
    const quint32 extAttr = qToLittleEndian(quint32(0));
    const quint32 relOffset = qToLittleEndian(static_cast<quint32>(localOffset));

    out.write(reinterpret_cast<const char*>(&centSig), 4);
    out.write(reinterpret_cast<const char*>(&verMade), 2);
    out.write(reinterpret_cast<const char*>(&verNeed), 2);
    out.write(reinterpret_cast<const char*>(&centFlags), 2);
    out.write(reinterpret_cast<const char*>(&centMethod), 2);
    out.write(reinterpret_cast<const char*>(&centModTime), 2);
    out.write(reinterpret_cast<const char*>(&centModDate), 2);
    out.write(reinterpret_cast<const char*>(&centCrc), 4);
    out.write(reinterpret_cast<const char*>(&centComp), 4);
    out.write(reinterpret_cast<const char*>(&centUncomp), 4);
    out.write(reinterpret_cast<const char*>(&centNameLen), 2);
    out.write(reinterpret_cast<const char*>(&centExtraLen), 2);
    out.write(reinterpret_cast<const char*>(&commentLen), 2);
    out.write(reinterpret_cast<const char*>(&diskStart), 2);
    out.write(reinterpret_cast<const char*>(&intAttr), 2);
    out.write(reinterpret_cast<const char*>(&extAttr), 4);
    out.write(reinterpret_cast<const char*>(&relOffset), 4);
    out.write(name);

    const qint64 endOffset = out.pos();
    const quint32 endSig = qToLittleEndian(quint32(0x06054b50));
    const quint16 diskNum = qToLittleEndian(quint16(0));
    const quint16 centDisk = qToLittleEndian(quint16(0));
    const quint16 entries = qToLittleEndian(quint16(1));
    const quint16 totalEntries = qToLittleEndian(quint16(1));
    const quint32 centDirSize = qToLittleEndian(static_cast<quint32>(endOffset - centralOffset));
    const quint32 centOffLe = qToLittleEndian(static_cast<quint32>(centralOffset));
    const quint16 zipCommentLen = qToLittleEndian(quint16(0));

    out.write(reinterpret_cast<const char*>(&endSig), 4);
    out.write(reinterpret_cast<const char*>(&diskNum), 2);
    out.write(reinterpret_cast<const char*>(&centDisk), 2);
    out.write(reinterpret_cast<const char*>(&entries), 2);
    out.write(reinterpret_cast<const char*>(&totalEntries), 2);
    out.write(reinterpret_cast<const char*>(&centDirSize), 4);
    out.write(reinterpret_cast<const char*>(&centOffLe), 4);
    out.write(reinterpret_cast<const char*>(&zipCommentLen), 2);

    return out.error() == QFile::NoError;
}
