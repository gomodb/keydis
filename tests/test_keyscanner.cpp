// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "test_keyscanner.h"

#include "core/keyspace/keyscanner.h"
#include "core/net/redisconnection.h"
#include "fakeserver.h"

#include <QHash>
#include <QTest>

namespace KeyDis::Test {

namespace {

QByteArray scanReply(const QList<QByteArray> &args)
{
    if (args.value(1) == QByteArrayLiteral("0")) {
        return QByteArrayLiteral("*2\r\n$1\r\n1\r\n*2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n");
    }
    return QByteArrayLiteral("*2\r\n$1\r\n0\r\n*2\r\n$3\r\nbaz\r\n$3\r\nqux\r\n");
}

QByteArray handler(const QList<QByteArray> &args, bool typeFilterSupported)
{
    if (args.isEmpty())
        return QByteArrayLiteral("-ERR empty\r\n");
    if (args[0] == QByteArrayLiteral("HELLO"))
        return QByteArrayLiteral("%1\r\n+server\r\n+redis\r\n");
    if (args[0] == QByteArrayLiteral("SELECT"))
        return QByteArrayLiteral("+OK\r\n");
    if (args[0] == QByteArrayLiteral("SCAN")) {
        if (args.contains(QByteArrayLiteral("TYPE")) && !typeFilterSupported)
            return QByteArrayLiteral("-ERR syntax error\r\n");
        return scanReply(args);
    }
    if (args[0] == QByteArrayLiteral("TYPE")) {
        if (args.value(1) == QByteArrayLiteral("bar"))
            return QByteArrayLiteral("+hash\r\n");
        if (args.value(1) == QByteArrayLiteral("baz"))
            return QByteArrayLiteral("+list\r\n");
        if (args.value(1) == QByteArrayLiteral("qux"))
            return QByteArrayLiteral("+zset\r\n");
        return QByteArrayLiteral("+string\r\n");
    }
    if (args[0] == QByteArrayLiteral("PTTL")) {
        if (args.value(1) == QByteArrayLiteral("bar"))
            return QByteArrayLiteral(":5000\r\n");
        if (args.value(1) == QByteArrayLiteral("baz"))
            return QByteArrayLiteral(":1000\r\n");
        return QByteArrayLiteral(":-1\r\n");
    }
    return QByteArrayLiteral("-ERR unknown\r\n");
}

} // namespace

void TestKeyScanner::scansAndLoadsMore()
{
    FakeRedisServer server;
    server.setHandler([](const QList<QByteArray> &args) { return handler(args, true); });
    QVERIFY(server.start());

    RedisConnection connection;
    connection.connectToHost(QStringLiteral("127.0.0.1"), server.port(), false, {}, {}, 0);
    QTRY_VERIFY_WITH_TIMEOUT(connection.isReady(), 5000);

    KeyScanner scanner(&connection);
    QVector<KeyInfo> first;
    QVector<KeyInfo> second;
    bool finished = false;
    QString error;

    connect(&scanner, &KeyScanner::batchReady, &scanner, [&](const QVector<KeyInfo> &keys) {
        if (first.isEmpty())
            first = keys;
        else
            second = keys;
    });
    connect(&scanner, &KeyScanner::progress, &scanner, [&](qint64, bool isFinished) {
        finished = isFinished;
    });
    connect(&scanner, &KeyScanner::errorOccurred, &scanner,
            [&](const QString &message) { error = message; });

    scanner.start(QByteArrayLiteral("*"), QString());
    QTRY_VERIFY_WITH_TIMEOUT(!first.isEmpty(), 5000);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(first.size(), 2);
    QCOMPARE(first[0].key, QByteArray("foo"));
    QCOMPARE(first[0].type, QStringLiteral("string"));
    QCOMPARE(first[0].ttlMs, qint64(-1));
    QCOMPARE(first[1].type, QStringLiteral("hash"));
    QCOMPARE(first[1].ttlMs, qint64(5000));

    scanner.loadMore();
    QTRY_VERIFY_WITH_TIMEOUT(finished, 5000);
    QCOMPARE(second.size(), 2);
    QCOMPARE(second[0].key, QByteArray("baz"));
    QCOMPARE(second[0].type, QStringLiteral("list"));
    QCOMPARE(second[1].key, QByteArray("qux"));
    QCOMPARE(second[1].type, QStringLiteral("zset"));
}

void TestKeyScanner::scansAllPages()
{
    FakeRedisServer server;
    server.setHandler([](const QList<QByteArray> &args) { return handler(args, true); });
    QVERIFY(server.start());

    RedisConnection connection;
    connection.connectToHost(QStringLiteral("127.0.0.1"), server.port(), false, {}, {}, 0);
    QTRY_VERIFY_WITH_TIMEOUT(connection.isReady(), 5000);

    KeyScanner scanner(&connection);
    QVector<KeyInfo> keys;
    bool finished = false;
    QString error;
    connect(&scanner, &KeyScanner::batchReady, &scanner,
            [&](const QVector<KeyInfo> &batch) { keys += batch; });
    connect(&scanner, &KeyScanner::progress, &scanner,
            [&](qint64, bool isFinished) { finished = isFinished; });
    connect(&scanner, &KeyScanner::errorOccurred, &scanner,
            [&](const QString &message) { error = message; });

    scanner.startAll(QByteArrayLiteral("*"), QString());
    QTRY_VERIFY_WITH_TIMEOUT(finished, 10000);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(keys.size(), 4);
    QCOMPARE(scanner.scanned(), qint64(4));
}

void TestKeyScanner::fallsBackWhenTypeFilterUnsupported()
{
    FakeRedisServer server;
    server.setHandler([](const QList<QByteArray> &args) { return handler(args, false); });
    QVERIFY(server.start());

    RedisConnection connection;
    connection.connectToHost(QStringLiteral("127.0.0.1"), server.port(), false, {}, {}, 0);
    QTRY_VERIFY_WITH_TIMEOUT(connection.isReady(), 5000);

    KeyScanner scanner(&connection);
    QVector<KeyInfo> keys;
    bool finished = false;
    QString error;
    connect(&scanner, &KeyScanner::batchReady, &scanner,
            [&](const QVector<KeyInfo> &batch) { keys += batch; });
    connect(&scanner, &KeyScanner::progress, &scanner,
            [&](qint64, bool isFinished) { finished = isFinished; });
    connect(&scanner, &KeyScanner::errorOccurred, &scanner,
            [&](const QString &message) { error = message; });

    scanner.start(QByteArrayLiteral("*"), QStringLiteral("hash"));
    QTRY_VERIFY_WITH_TIMEOUT(keys.size() == 1, 5000);
    QCOMPARE(keys[0].key, QByteArray("bar"));
    QCOMPARE(keys[0].type, QStringLiteral("hash"));
    scanner.loadMore();
    QTRY_VERIFY_WITH_TIMEOUT(finished, 5000);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    bool sawType = false;
    bool sawFallback = false;
    for (const auto &command : server.received()) {
        if (command.value(0) != QByteArrayLiteral("SCAN"))
            continue;
        if (command.contains(QByteArrayLiteral("TYPE")))
            sawType = true;
        else
            sawFallback = true;
    }
    QVERIFY(sawType);
    QVERIFY(sawFallback);
}

} // namespace KeyDis::Test
