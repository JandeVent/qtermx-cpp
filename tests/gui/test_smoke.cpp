// Smoke test proving the Qt Test (QTest) harness works for Qt-touching code.
// Real widget/renderer tests land in Phase 4.
#include <QtTest>

class TestSmoke : public QObject
{
    Q_OBJECT

private slots:
    void stringBasics();
    void byteArrayRoundTrip();
};

void TestSmoke::stringBasics()
{
    QString s = QStringLiteral("qtermx");
    QCOMPARE(s.toUpper(), QStringLiteral("QTERMX"));
    QVERIFY(s.startsWith(QStringLiteral("qt")));
}

void TestSmoke::byteArrayRoundTrip()
{
    QByteArray bytes = QByteArrayLiteral("\x1b[31mred");
    QCOMPARE(bytes.size(), 8);
    QCOMPARE(bytes.at(0), '\x1b');
}

QTEST_MAIN(TestSmoke)
#include "test_smoke.moc"