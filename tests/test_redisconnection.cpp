// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "test_redisconnection.h"

#include "core/net/redisconnection.h"
#include "fakeserver.h"

#include <QHash>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QTest>

namespace KeyDis::Test {

namespace {

bool takeUnixCommand(QByteArray *buffer, QList<QByteArray> *args)
{
    if (!buffer->startsWith('*'))
        return false;
    const qsizetype countEnd = buffer->indexOf("\r\n", 1);
    if (countEnd < 0)
        return false;
    bool ok = false;
    const int count = buffer->mid(1, countEnd - 1).toInt(&ok);
    if (!ok || count < 0)
        return false;
    qsizetype pos = countEnd + 2;
    QList<QByteArray> parsed;
    for (int index = 0; index < count; ++index) {
        if (pos >= buffer->size() || buffer->at(pos) != '$')
            return false;
        const qsizetype lenEnd = buffer->indexOf("\r\n", pos + 1);
        if (lenEnd < 0)
            return false;
        const int len = buffer->mid(pos + 1, lenEnd - pos - 1).toInt(&ok);
        if (!ok || len < 0)
            return false;
        pos = lenEnd + 2;
        if (buffer->size() < pos + len + 2)
            return false;
        parsed.append(buffer->mid(pos, len));
        pos += len + 2;
    }
    buffer->remove(0, pos);
    *args = parsed;
    return true;
}

class FakeUnixRedisServer
{
public:
    FakeUnixRedisServer()
    {
        m_server.listen(m_directory.filePath(QStringLiteral("redis.sock")));
        QObject::connect(&m_server, &QLocalServer::newConnection, [this] {
            while (QLocalSocket *socket = m_server.nextPendingConnection()) {
                QObject::connect(socket, &QLocalSocket::readyRead, socket, [this, socket] {
                    QByteArray &buffer = m_input[socket];
                    buffer += socket->readAll();
                    QList<QByteArray> args;
                    while (takeUnixCommand(&buffer, &args)) {
                        QByteArray reply;
                        if (args.value(0) == QByteArrayLiteral("HELLO"))
                            reply = "%1\r\n+server\r\n+redis\r\n";
                        else if (args.value(0) == QByteArrayLiteral("SELECT"))
                            reply = "+OK\r\n";
                        else if (args.value(0) == QByteArrayLiteral("PING"))
                            reply = "+PONG\r\n";
                        else
                            reply = "-ERR unknown command\r\n";
                        socket->write(reply);
                        socket->flush();
                    }
                });
            }
        });
    }

    [[nodiscard]] QString path() const
    {
        return m_directory.filePath(QStringLiteral("redis.sock"));
    }

    [[nodiscard]] bool listening() const
    {
        return m_server.isListening();
    }

private:
    QTemporaryDir m_directory;
    QLocalServer m_server;
    QHash<QLocalSocket *, QByteArray> m_input;
};

} // namespace

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

void TestRedisConnection::rejectsMissingTlsFiles()
{
    RedisConnection connection;
    TlsConfig config;
    config.enabled = true;
    config.caFile = QStringLiteral("/nonexistent/keydis-ca.pem");
    connection.setTlsConfig(config);
    QString error;
    connect(&connection, &RedisConnection::errorOccurred, &connection,
            [&](const QString &message) { error = message; });
    connection.connectToHost(QStringLiteral("127.0.0.1"), 1, true, {}, {}, 0);
    QTRY_VERIFY_WITH_TIMEOUT(!error.isEmpty(), 5000);
    QVERIFY(error.contains(QStringLiteral("TLS CA")));
}

void TestRedisConnection::rejectsMissingUnixSocket()
{
    RedisConnection connection;
    QString error;
    connect(&connection, &RedisConnection::errorOccurred, &connection,
            [&](const QString &message) { error = message; });
    connection.connectToUnixSocket(QStringLiteral("/tmp/keydis-nonexistent.sock"), {}, {}, 0);
    QTRY_VERIFY_WITH_TIMEOUT(!error.isEmpty(), 5000);
    QVERIFY(error.contains(QStringLiteral("Unix socket")));
}

void TestRedisConnection::unixSocketHandshake()
{
    FakeUnixRedisServer server;
    QVERIFY(server.listening());
    RedisConnection connection;
    QString error;
    RespValue reply;
    bool gotReply = false;
    connect(&connection, &RedisConnection::errorOccurred, &connection,
            [&](const QString &message) { error = message; });
    connect(&connection, &RedisConnection::replyReady, &connection,
            [&](const RespValue &value) {
                reply = value;
                gotReply = true;
            });
    connection.connectToUnixSocket(server.path(), {}, {}, 2);
    QTRY_VERIFY_WITH_TIMEOUT(connection.isReady() || !error.isEmpty(), 5000);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    connection.sendCommand({QByteArrayLiteral("PING")});
    QTRY_VERIFY_WITH_TIMEOUT(gotReply, 5000);
    QCOMPARE(reply.scalar, QByteArrayLiteral("PONG"));
}

} // namespace KeyDis::Test
