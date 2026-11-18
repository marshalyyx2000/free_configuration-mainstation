#include "event/event_format.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>

class TestEventFormat : public QObject {
    Q_OBJECT
private slots:
    void cdcArrayUsesStVal()
    {
        QJsonArray cdc;
        cdc.append(false);
        cdc.append(0);
        cdc.append(QStringLiteral("2026-11-18T12:00:00.000Z"));
        QCOMPARE(formatReportValue(cdc), QStringLiteral("false"));

        QJsonArray mag;
        mag.append(12.5);
        mag.append(0);
        mag.append(QStringLiteral("t"));
        QCOMPARE(formatReportValue(mag), QStringLiteral("12.5"));
    }

    void cdcObjectUsesStVal()
    {
        QJsonObject o;
        o.insert(QStringLiteral("q"), 0);
        o.insert(QStringLiteral("stVal"), true);
        QCOMPARE(formatReportValue(o), QStringLiteral("true"));
    }

    void emptyArrayStaysEmpty()
    {
        QCOMPARE(formatReportValue(QJsonArray()), QString());
    }

    void giReasonIsKept()
    {
        QCOMPARE(formatReportReason(16), QStringLiteral("总召唤"));
        QCOMPARE(formatReportReason(0), QStringLiteral("-"));
    }
};

QTEST_GUILESS_MAIN(TestEventFormat)
#include "test_event_format.moc"
