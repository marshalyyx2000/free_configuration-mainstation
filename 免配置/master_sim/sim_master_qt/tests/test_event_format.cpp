#include "event/event_format.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>

class TestEventFormat : public QObject {
    Q_OBJECT
private slots:
    void cdcArrayKeepsQualityAndTime()
    {
        QJsonArray cdc;
        cdc.append(false);
        cdc.append(QStringLiteral("0000000000000"));
        cdc.append(QStringLiteral("2026-11-18T12:00:00.000Z"));
        QCOMPARE(formatReportValue(cdc),
            QStringLiteral("false | 0000000000000 | 2026-11-18T12:00:00.000Z"));

        const QStringList spc{ QStringLiteral("stVal"), QStringLiteral("q"), QStringLiteral("t") };
        QCOMPARE(formatReportMembers(cdc, spc),
            QStringLiteral("stVal=false q=0000000000000 t=2026-11-18T12:00:00.000Z"));
    }

    void singleDaReferenceDoesNotInventQuality()
    {
        const QStringList onlySt{ QStringLiteral("stVal") };
        QCOMPARE(formatReportMembers(false, onlySt), QStringLiteral("stVal=false"));
        QJsonArray wrapped;
        wrapped.append(false);
        QCOMPARE(formatReportMembers(wrapped, onlySt), QStringLiteral("stVal=false"));
    }

    void extraArrayPartsStayVisible()
    {
        QJsonArray cdc;
        cdc.append(false);
        cdc.append(QStringLiteral("qbits"));
        cdc.append(QStringLiteral("ts"));
        const QStringList onlySt{ QStringLiteral("stVal") };
        QCOMPARE(formatReportMembers(cdc, onlySt), QStringLiteral("stVal=false qbits ts"));
    }

    void nestedStructMembers()
    {
        QJsonArray mag;
        mag.append(12.5);
        mag.append(0);
        QJsonArray cdc;
        cdc.append(mag);
        cdc.append(QStringLiteral("qbits"));
        cdc.append(QStringLiteral("ts"));
        const QStringList mx{ QStringLiteral("mag{f,i}"), QStringLiteral("q"), QStringLiteral("t") };
        QCOMPARE(formatReportMembers(cdc, mx), QStringLiteral("mag={f=12.5 i=0} q=qbits t=ts"));
    }

    void objectKeepsEveryMember()
    {
        QJsonObject o;
        o.insert(QStringLiteral("q"), QStringLiteral("qbits"));
        o.insert(QStringLiteral("stVal"), true);
        o.insert(QStringLiteral("t"), QStringLiteral("ts"));
        QCOMPARE(formatReportValue(o), QStringLiteral("q=qbits stVal=true t=ts"));
        const QStringList spc{ QStringLiteral("stVal"), QStringLiteral("q"), QStringLiteral("t") };
        QCOMPARE(formatReportMembers(o, spc), QStringLiteral("stVal=true q=qbits t=ts"));
    }

    void emptyArrayStaysEmpty()
    {
        QCOMPARE(formatReportValue(QJsonArray()), QString());
        QCOMPARE(formatReportMembers(QJsonArray(), { QStringLiteral("stVal"), QStringLiteral("q"), QStringLiteral("t") }),
            QString());
    }

    void giReasonIsKept()
    {
        QCOMPARE(formatReportReason(16), QStringLiteral("总召唤"));
        QCOMPARE(formatReportReason(0), QStringLiteral("-"));
    }
};

QTEST_GUILESS_MAIN(TestEventFormat)
#include "test_event_format.moc"
