// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "test_redisconnection.h"

#include "core/net/redisconnection.h"
#include "fakeserver.h"

#include <QTest>

namespace KeyDis::Test {

void TestRedisConnection::handshakeSelectAndPing()
{
    FakeRedisServer server;
    QVERIFY(server.start());

    RedisConnection connection;
    QString error;
    bool gotReply = false;
    RespValue reply;

    connect(&connection, &RedisConnection::errorOccurred, &connection,
            [&](const QString &message) { error = message; });
    connect(&connection, &RedisConnection::replyReady, &connection,
            [&](const RespValue &value) {
                reply = value;
                gotReply = true;
            });

    connection.connectToHost(QStringLiteral("127.0.0.1"), server.port(), false, {}, {}, 2);
    QTRY_VERIFY_WITH_TIMEOUT(connection.isReady(), 5000);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(server.received().size() >= 2, 5000);
    QCOMPARE(server.received()[0].value(0), QByteArray("HELLO"));
    QCOMPARE(server.received()[1].value(0), QByteArray("SELECT"));
    QCOMPARE(server.received()[1].value(1), QByteArray("2"));

    connection.sendCommand({QByteArrayLiteral("PING")});
    QTRY_VERIFY_WITH_TIMEOUT(gotReply, 5000);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(reply.type == RespType::SimpleString);
    QCOMPARE(reply.scalar, QByteArray("PONG"));
}

void TestRedisConnection::fallsBackToResp2()
{
    FakeRedisServer server;
    bool sawHello = false;
    bool sawAuth = false;
    server.setHandler([&](const QList<QByteArray> &args) {
        if (args.value(0) == QByteArrayLiteral("HELLO")) {
            sawHello = true;
            return QByteArrayLiteral("-ERR unknown command 'HELLO'\r\n");
        }
        if (args.value(0) == QByteArrayLiteral("AUTH")) {
            sawAuth = true;
            return QByteArrayLiteral("+OK\r\n");
        }
        if (args.value(0) == QByteArrayLiteral("PING"))
            return QByteArrayLiteral("+PONG\r\n");
        if (args.value(0) == QByteArrayLiteral("SELECT"))
            return QByteArrayLiteral("+OK\r\n");
        return QByteArrayLiteral("-ERR unknown command\r\n");
    });
    QVERIFY(server.start());

    RedisConnection connection;
    QString error;
    bool gotReply = false;
    RespValue reply;
    connect(&connection, &RedisConnection::errorOccurred, &connection,
            [&](const QString &message) { error = message; });
    connect(&connection, &RedisConnection::replyReady, &connection,
            [&](const RespValue &value) {
                reply = value;
                gotReply = true;
            });

    connection.connectToHost(QStringLiteral("127.0.0.1"), server.port(), false,
                             QStringLiteral("user"), QStringLiteral("secret"), 0);
    QTRY_VERIFY_WITH_TIMEOUT(connection.isReady(), 5000);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(sawHello);
    QVERIFY(sawAuth);

    connection.sendCommand({QByteArrayLiteral("PING")});
    QTRY_VERIFY_WITH_TIMEOUT(gotReply, 5000);
    QVERIFY(reply.type == RespType::SimpleString);
    QCOMPARE(reply.scalar, QByteArray("PONG"));
}

} // namespace KeyDis::Test
