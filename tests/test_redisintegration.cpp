// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "test_redisintegration.h"

#include "core/analysis/memoryanalyzer.h"
#include "core/conn/sentinelresolver.h"
#include "core/keyspace/keyscanner.h"
#include "core/transfer/keytransfer.h"
#include "core/net/redisconnection.h"

#include <QEventLoop>
#include <QTest>
#include <QTemporaryDir>
#include <QTimer>

#include <utility>

namespace KeyDis::Test {

namespace {

RespValue command(RedisConnection &connection, const QList<QByteArray> &args, QString *error)
{
    RespValue result;
    bool done = false;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    connection.sendCommand(args, [&](const RespValue &reply, const QString &replyError) {
        result = reply;
        if (error)
            *error = replyError;
        done = true;
        loop.quit();
    });
    timer.start(5000);
    if (!done)
        loop.exec();
    if (!done && error)
        *error = QStringLiteral("timeout");
    return result;
}

} // namespace

void TestRedisIntegration::realServerRoundTrip()
{
    bool ok = false;
    const int port = qEnvironmentVariableIntValue("KEYDIS_REDIS_PORT", &ok);
    if (!ok || port <= 0)
        QSKIP("KEYDIS_REDIS_PORT is not set");

    RedisConnection connection;
    QString error;
    connection.connectToHost(QStringLiteral("127.0.0.1"), static_cast<quint16>(port), false, {},
                             {}, 0);
    QTRY_VERIFY_WITH_TIMEOUT(connection.isReady(), 5000);

    const QByteArray stringKey("keydis:it:string");
    const QByteArray hashKey("keydis:it:hash");
    const QByteArray listKey("keydis:it:list");
    const QByteArray setKey("keydis:it:set");
    const QByteArray zsetKey("keydis:it:zset");

    command(connection, {QByteArrayLiteral("DEL"), stringKey, hashKey, listKey, setKey, zsetKey},
            &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    RespValue reply = command(connection, {QByteArrayLiteral("SET"), stringKey,
                                           QByteArrayLiteral("hello")}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(!reply.isError());
    reply = command(connection, {QByteArrayLiteral("GET"), stringKey}, &error);
    QCOMPARE(reply.scalar, QByteArray("hello"));

    command(connection, {QByteArrayLiteral("HSET"), hashKey, QByteArrayLiteral("name"),
                         QByteArrayLiteral("Alice"), QByteArrayLiteral("age"),
                         QByteArrayLiteral("30")}, &error);
    reply = command(connection, {QByteArrayLiteral("HGETALL"), hashKey}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(reply.type == RespType::Map);
    QCOMPARE(reply.pairs.size(), size_t(2));

    command(connection, {QByteArrayLiteral("RPUSH"), listKey, QByteArrayLiteral("a"),
                         QByteArrayLiteral("b"), QByteArrayLiteral("c")}, &error);
    reply = command(connection, {QByteArrayLiteral("LRANGE"), listKey, QByteArrayLiteral("0"),
                                 QByteArrayLiteral("-1")}, &error);
    QCOMPARE(reply.elements.size(), size_t(3));
    QCOMPARE(reply.elements[1].scalar, QByteArray("b"));

    command(connection, {QByteArrayLiteral("SADD"), setKey, QByteArrayLiteral("x"),
                         QByteArrayLiteral("y")}, &error);
    reply = command(connection, {QByteArrayLiteral("SMEMBERS"), setKey}, &error);
    QCOMPARE(reply.elements.size(), size_t(2));

    command(connection, {QByteArrayLiteral("ZADD"), zsetKey, QByteArrayLiteral("10"),
                         QByteArrayLiteral("alice"), QByteArrayLiteral("20"),
                         QByteArrayLiteral("bob")}, &error);
    reply = command(connection, {QByteArrayLiteral("ZRANGE"), zsetKey, QByteArrayLiteral("0"),
                                 QByteArrayLiteral("-1"), QByteArrayLiteral("WITHSCORES")},
                    &error);
    if (reply.elements.size() == 2 && reply.elements[0].type == RespType::Array) {
        QCOMPARE(reply.elements[0].elements.size(), size_t(2));
        QCOMPARE(reply.elements[0].elements[0].scalar, QByteArray("alice"));
    } else {
        QCOMPARE(reply.elements.size(), size_t(4));
    }

    reply = command(connection, {QByteArrayLiteral("EXPIRE"), stringKey, QByteArrayLiteral("120")},
                    &error);
    QCOMPARE(reply.integer, qint64(1));
    reply = command(connection, {QByteArrayLiteral("PTTL"), stringKey}, &error);
    QVERIFY(reply.integer > 0);

    KeyScanner scanner(&connection);
    QVector<KeyInfo> keys;
    bool finished = false;
    QString scanError;
    connect(&scanner, &KeyScanner::batchReady, &scanner,
            [&](const QVector<KeyInfo> &batch) { keys += batch; });
    connect(&scanner, &KeyScanner::progress, &scanner,
            [&](qint64, bool isFinished) { finished = isFinished; });
    connect(&scanner, &KeyScanner::errorOccurred, &scanner,
            [&](const QString &message) { scanError = message; });
    scanner.start(QByteArrayLiteral("keydis:it:*"), QString());
    QTRY_VERIFY_WITH_TIMEOUT(finished, 10000);
    QVERIFY2(scanError.isEmpty(), qPrintable(scanError));
    QCOMPARE(keys.size(), 5);

    bool sawHash = false;
    for (const KeyInfo &info : std::as_const(keys)) {
        if (info.key == hashKey)
            sawHash = info.type == QStringLiteral("hash");
    }
    QVERIFY(sawHash);

    command(connection, {QByteArrayLiteral("DEL"), stringKey, hashKey, listKey, setKey, zsetKey},
            &error);
}

void TestRedisIntegration::pubSubRoundTrip()
{
    bool ok = false;
    const int port = qEnvironmentVariableIntValue("KEYDIS_REDIS_PORT", &ok);
    if (!ok || port <= 0)
        QSKIP("KEYDIS_REDIS_PORT is not set");

    RedisConnection publisher;
    publisher.connectToHost(QStringLiteral("127.0.0.1"), static_cast<quint16>(port), false, {},
                            {}, 0);
    QTRY_VERIFY_WITH_TIMEOUT(publisher.isReady(), 5000);

    RedisConnection subscriber;
    subscriber.connectToHost(QStringLiteral("127.0.0.1"), static_cast<quint16>(port), false, {},
                             {}, 0);
    QTRY_VERIFY_WITH_TIMEOUT(subscriber.isReady(), 5000);

    bool gotMessage = false;
    QString error;
    auto onMessage = [&](const RespValue &reply) {
        if (reply.toDisplayString().contains(QStringLiteral("keydis:it:channel")))
            gotMessage = true;
    };
    QObject::connect(&subscriber, &RedisConnection::replyReady, &subscriber, onMessage);
    QObject::connect(&subscriber, &RedisConnection::pushReceived, &subscriber, onMessage);
    QObject::connect(&subscriber, &RedisConnection::errorOccurred, &subscriber,
                     [&](const QString &message) { error = message; });

    subscriber.sendCommand({QByteArrayLiteral("SUBSCRIBE"),
                            QByteArrayLiteral("keydis:it:channel")});
    QTest::qWait(150);
    const RespValue published =
        command(publisher, {QByteArrayLiteral("PUBLISH"),
                            QByteArrayLiteral("keydis:it:channel"),
                            QByteArrayLiteral("hello")},
                &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(published.integer, qint64(1));
    QTRY_VERIFY_WITH_TIMEOUT(gotMessage, 5000);
    subscriber.disconnectFromHost();
}

void TestRedisIntegration::exportImportRoundTrip()
{
    bool ok = false;
    const int port = qEnvironmentVariableIntValue("KEYDIS_REDIS_PORT", &ok);
    if (!ok || port <= 0)
        QSKIP("KEYDIS_REDIS_PORT is not set");

    RedisConnection connection;
    connection.connectToHost(QStringLiteral("127.0.0.1"), static_cast<quint16>(port), false, {},
                             {}, 0);
    QTRY_VERIFY_WITH_TIMEOUT(connection.isReady(), 5000);

    const QByteArray keyA("keydis:it:export:a");
    const QByteArray keyB("keydis:it:export:b");
    QString error;
    command(connection, {QByteArrayLiteral("DEL"), keyA, keyB}, &error);
    command(connection, {QByteArrayLiteral("SET"), keyA, QByteArrayLiteral("alpha")}, &error);
    command(connection, {QByteArrayLiteral("HSET"), keyB, QByteArrayLiteral("field"),
                         QByteArrayLiteral("beta")}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("export.jsonl"));

    KeyTransfer transfer(&connection);
    bool finished = false;
    bool success = false;
    QString message;
    QObject::connect(&transfer, &KeyTransfer::finished, &transfer,
                     [&](bool ok, const QString &text) {
        finished = true;
        success = ok;
        message = text;
    });

    transfer.exportToFile(path, QByteArrayLiteral("keydis:it:export:*"));
    QTRY_VERIFY_WITH_TIMEOUT(finished, 10000);
    QVERIFY2(success, qPrintable(message));

    command(connection, {QByteArrayLiteral("DEL"), keyA, keyB}, &error);
    command(connection, {QByteArrayLiteral("GET"), keyA}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    finished = false;
    transfer.importFromFile(path);
    QTRY_VERIFY_WITH_TIMEOUT(finished, 10000);
    QVERIFY2(success, qPrintable(message));

    const RespValue stringValue = command(connection, {QByteArrayLiteral("GET"), keyA}, &error);
    QCOMPARE(stringValue.scalar, QByteArray("alpha"));
    const RespValue hashValue = command(connection, {QByteArrayLiteral("HGET"), keyB,
                                                     QByteArrayLiteral("field")},
                                        &error);
    QCOMPARE(hashValue.scalar, QByteArray("beta"));

    command(connection, {QByteArrayLiteral("DEL"), keyA, keyB}, &error);
}

void TestRedisIntegration::memoryAnalysisRoundTrip()
{
    bool ok = false;
    const int port = qEnvironmentVariableIntValue("KEYDIS_REDIS_PORT", &ok);
    if (!ok || port <= 0)
        QSKIP("KEYDIS_REDIS_PORT is not set");

    RedisConnection connection;
    connection.connectToHost(QStringLiteral("127.0.0.1"), static_cast<quint16>(port), false, {},
                             {}, 0);
    QTRY_VERIFY_WITH_TIMEOUT(connection.isReady(), 5000);

    const QByteArray keyA("keydis:it:mem:a");
    const QByteArray keyB("keydis:it:mem:b");
    QString error;
    command(connection, {QByteArrayLiteral("DEL"), keyA, keyB}, &error);
    command(connection, {QByteArrayLiteral("SET"), keyA, QByteArray(2000, 'x')}, &error);
    command(connection, {QByteArrayLiteral("HSET"), keyB, QByteArrayLiteral("field"),
                         QByteArrayLiteral("value")}, &error);
    command(connection, {QByteArrayLiteral("PEXPIRE"), keyB, QByteArrayLiteral("60000")},
            &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    MemoryAnalyzer analyzer(&connection);
    bool finished = false;
    bool success = false;
    QString message;
    MemoryAnalysisResult result;
    QObject::connect(&analyzer, &MemoryAnalyzer::finished, &analyzer,
                     [&](bool ok, const MemoryAnalysisResult &value, const QString &text) {
        finished = true;
        success = ok;
        result = value;
        message = text;
    });
    analyzer.start(QByteArrayLiteral("keydis:it:mem:*"));
    QTRY_VERIFY_WITH_TIMEOUT(finished, 10000);
    QVERIFY2(success, qPrintable(message));
    QCOMPARE(result.keyCount, 2);
    QVERIFY(result.totalBytes > 0);
    QVERIFY(result.keys.size() >= 2);
    QVERIFY(result.keys.at(0).bytes >= result.keys.at(1).bytes);
    QVERIFY(result.countByType.contains(QStringLiteral("string")));
    QVERIFY(result.countByType.contains(QStringLiteral("hash")));

    command(connection, {QByteArrayLiteral("DEL"), keyA, keyB}, &error);
}

void TestRedisIntegration::sentinelResolvesMaster()
{
    bool ok = false;
    const int sentinelPort = qEnvironmentVariableIntValue("KEYDIS_SENTINEL_PORT", &ok);
    if (!ok || sentinelPort <= 0)
        QSKIP("KEYDIS_SENTINEL_PORT is not set");

    SentinelResolver resolver;
    bool finished = false;
    bool success = false;
    QString resolvedHost;
    quint16 resolvedPort = 0;
    QString error;
    QObject::connect(&resolver, &SentinelResolver::resolved, &resolver,
                     [&](const QString &host, quint16 port) {
        finished = true;
        success = true;
        resolvedHost = host;
        resolvedPort = port;
    });
    QObject::connect(&resolver, &SentinelResolver::failed, &resolver,
                     [&](const QString &message) {
        finished = true;
        success = false;
        error = message;
    });
    resolver.resolve(QStringLiteral("127.0.0.1"), static_cast<quint16>(sentinelPort), false, {},
                     {}, QStringLiteral("mymaster"));
    QTRY_VERIFY_WITH_TIMEOUT(finished, 10000);
    QVERIFY2(success, qPrintable(error));
    QCOMPARE(resolvedHost, QStringLiteral("127.0.0.1"));
    QCOMPARE(resolvedPort, quint16(6399));
}

} // namespace KeyDis::Test
