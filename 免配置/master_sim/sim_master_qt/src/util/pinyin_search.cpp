#include "util/pinyin_search.h"

#include <QChar>
#include <QRegularExpression>
#include <QStringEncoder>

namespace {

char firstLetterFromGbkCode(int code)
{
    if (code >= 45217 && code <= 45252)
        return 'a';
    if (code >= 45253 && code <= 45760)
        return 'b';
    if (code >= 45761 && code <= 46317)
        return 'c';
    if (code >= 46318 && code <= 46825)
        return 'd';
    if (code >= 46826 && code <= 47009)
        return 'e';
    if (code >= 47010 && code <= 47296)
        return 'f';
    if (code >= 47297 && code <= 47613)
        return 'g';
    if (code >= 47614 && code <= 48118)
        return 'h';
    if (code >= 48119 && code <= 49061)
        return 'j';
    if (code >= 49062 && code <= 49323)
        return 'k';
    if (code >= 49324 && code <= 49895)
        return 'l';
    if (code >= 49896 && code <= 50370)
        return 'm';
    if (code >= 50371 && code <= 50613)
        return 'n';
    if (code >= 50614 && code <= 50621)
        return 'o';
    if (code >= 50622 && code <= 50905)
        return 'p';
    if (code >= 50906 && code <= 51386)
        return 'q';
    if (code >= 51387 && code <= 51445)
        return 'r';
    if (code >= 51446 && code <= 52217)
        return 's';
    if (code >= 52218 && code <= 52697)
        return 't';
    if (code >= 52698 && code <= 52979)
        return 'w';
    if (code >= 52980 && code <= 53640)
        return 'x';
    if (code >= 53689 && code <= 54480)
        return 'y';
    if (code >= 54481 && code <= 55289)
        return 'z';
    return '\0';
}

char chineseFirstLetter(QChar ch)
{
    static QStringEncoder encoder(QStringEncoder::System);
    const QByteArray bytes = encoder.encode(QString(ch));
    if (bytes.isEmpty())
        return '\0';
    if (bytes.size() == 1)
        return static_cast<char>(bytes.at(0));
    const int code = (static_cast<unsigned char>(bytes.at(0)) << 8)
        | static_cast<unsigned char>(bytes.at(1));
    return firstLetterFromGbkCode(code);
}

bool isAsciiLettersOrDigits(const QString& segment)
{
    if (segment.isEmpty())
        return false;
    for (const QChar& ch : segment) {
        if (!ch.isLetterOrNumber())
            return false;
    }
    return true;
}

} // namespace

QString pinyinInitials(const QString& text)
{
    QString result;
    result.reserve(text.size());
    for (const QChar& ch : text) {
        if (!ch.isLetterOrNumber())
            continue;
        if (ch.unicode() >= 0x4E00 && ch.unicode() <= 0x9FFF) {
            const char letter = chineseFirstLetter(ch);
            if (letter != '\0')
                result += QChar::fromLatin1(letter);
        } else {
            result += ch.toLower();
        }
    }
    return result;
}

bool matchesSearchSegment(const QString& text, const QString& segment)
{
    const QString trimmed = segment.trimmed();
    if (trimmed.isEmpty())
        return true;
    if (text.contains(trimmed, Qt::CaseInsensitive))
        return true;
    if (isAsciiLettersOrDigits(trimmed)) {
        const QString initials = pinyinInitials(text);
        if (initials.contains(trimmed, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

bool matchesMultiSegmentSearch(const QString& text, const QString& query)
{
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty())
        return true;
    const QStringList segments = trimmed.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    for (const QString& segment : segments) {
        if (!matchesSearchSegment(text, segment))
            return false;
    }
    return true;
}
