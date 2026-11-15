#ifndef PINYIN_SEARCH_H
#define PINYIN_SEARCH_H

#include <QString>

QString pinyinInitials(const QString& text);
bool matchesSearchSegment(const QString& text, const QString& segment);
bool matchesMultiSegmentSearch(const QString& text, const QString& query);

#endif
