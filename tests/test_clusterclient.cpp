// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "test_clusterclient.h"

#include "core/keyspace/keyscanner.h"
#include "core/net/clusterclient.h"

#include <QElapsedTimer>
#include <QTest>

namespace KeyDis::Test {

namespace {

RespValue command(ClusterClient &client, const QList<QByteArray> &args, QString *error)
{
    RespValue result;
    bool done = false;
    client.sendCommand(args, [&](const RespValue &reply, const QString &message) {
        result = reply;
        if (error)
            *error = message;
        done = true;
    });
    QElapsedTimer timer;
    timer.start();
    while (!done && timer.elapsed() < 10000)
        QTest::qWait(10);
    if (!done && error)
        *error = QStringLiteral("command timed out");
    return result;
}

} // namespace

void TestClusterClient::routesKeysAndScans()
{
    bool ok = false;
    const int port = qEnvironmentVariableIntValue("KEYDIS_CLUSTER_PORT", &ok);
    if (!ok || port <= 0)
        QSKIP("KEYDIS_CLUSTER_PORT is not set");

    ClusterClient client;
    bool ready = false;
    QString connectionError;
    QObject::connect(&client, &RedisClient::stateChanged, &client,
                     [&](const QString &state) {
        if (state == QLatin1String("Ready"))
            ready = true;
    });
    QObject::connect(&client, &RedisClient::errorOccurred, &client,
                     [&](const QString &message) { connectionError = message; });
    client.connectToHost(QStringLiteral("127.0.0.1"), static_cast<quint16>(port), false, {},
                         {}, 0);
    QTRY_VERIFY_WITH_TIMEOUT(ready, 15000);
    QVERIFY2(connectionError.isEmpty(), qPrintable(connectionError));

    QString error;
    const QByteArray keyA("cluster:it:a");
    const QByteArray keyB("cluster:it:b");
    const QByteArray keyC("cluster:it:c");
    command(client, {QByteArrayLiteral("DEL"), keyA, keyB, keyC}, &error);
    command(client, {QByteArrayLiteral("SET"), keyA, QByteArrayLiteral("one")}, &error);
    command(client, {QByteArrayLiteral("SET"), keyB, QByteArrayLiteral("two")}, &error);
    command(client, {QByteArrayLiteral("SET"), keyC, QByteArrayLiteral("three")}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QCOMPARE(command(client, {QByteArrayLiteral("GET"), keyA}, &error).scalar,
             QByteArray("one"));
    QCOMPARE(command(client, {QByteArrayLiteral("GET"), keyB}, &error).scalar,
             QByteArray("two"));
    QCOMPARE(command(client, {QByteArrayLiteral("GET"), keyC}, &error).scalar,
             QByteArray("three"));

    const RespValue crossSlot =
        command(client, {QByteArrayLiteral("MGET"), keyA, keyB, keyC}, &error);
    Q_UNUSED(crossSlot)
    QVERIFY(error.contains(QStringLiteral("CROSSSLOT")));

    KeyScanner scanner(&client);
    QVector<KeyInfo> keys;
    bool finished = false;
    QString scanError;
    QObject::connect(&scanner, &KeyScanner::batchReady, &scanner,
                     [&](const QVector<KeyInfo> &batch) { keys += batch; });
    QObject::connect(&scanner, &KeyScanner::progress, &scanner,
                     [&](qint64, bool done) { finished = done; });
    QObject::connect(&scanner, &KeyScanner::errorOccurred, &scanner,
                     [&](const QString &message) { scanError = message; });
    scanner.startAll(QByteArrayLiteral("cluster:it:*"));
    QTRY_VERIFY_WITH_TIMEOUT(finished, 15000);
    QVERIFY2(scanError.isEmpty(), qPrintable(scanError));
    QVERIFY(keys.size() >= 3);

    command(client, {QByteArrayLiteral("DEL"), keyA, keyB, keyC}, &error);
    client.disconnectFromHost();
}

void TestClusterClient::failsGracefully()
{
    ClusterClient client;
    bool failed = false;
    QString error;
    QObject::connect(&client, &RedisClient::errorOccurred, &client,
                     [&](const QString &message) { failed = true; error = message; });
    client.connectToHost(QStringLiteral("127.0.0.1"), 1, false, {}, {}, 0);
    QTRY_VERIFY_WITH_TIMEOUT(failed, 10000);
    QVERIFY(!error.isEmpty());
    client.disconnectFromHost();
}

} // namespace KeyDis::Test
