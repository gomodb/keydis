// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "fakeserver.h"

#include <QHostAddress>
#include <QTcpSocket>

#include <utility>

namespace KeyDis::Test {

namespace {

bool takeCommand(QByteArray *buffer, QList<QByteArray> *args)
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

QByteArray defaultReply(const QList<QByteArray> &args)
{
    if (args.isEmpty())
        return "-ERR empty command\r\n";
    if (args[0] == QByteArrayLiteral("HELLO"))
        return "%1\r\n+server\r\n+redis\r\n";
    if (args[0] == QByteArrayLiteral("SELECT"))
        return "+OK\r\n";
    if (args[0] == QByteArrayLiteral("PING"))
        return "+PONG\r\n";
    return "-ERR unknown command\r\n";
}

} // namespace

FakeRedisServer::FakeRedisServer(QObject *parent)
    : QObject(parent)
    , m_handler(defaultReply)
{
    connect(&m_server, &QTcpServer::newConnection, this, [this] {
        while (QTcpSocket *socket = m_server.nextPendingConnection()) {
            connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                m_input += socket->readAll();
                QList<QByteArray> args;
                while (takeCommand(&m_input, &args))
                    handleCommand(socket, args);
            });
        }
    });
}

bool FakeRedisServer::start()
{
    return m_server.listen(QHostAddress::LocalHost, 0);
}

void FakeRedisServer::stop()
{
    m_server.close();
    const auto sockets = m_server.findChildren<QTcpSocket *>();
    for (QTcpSocket *socket : sockets) {
        socket->disconnectFromHost();
        socket->deleteLater();
    }
}

quint16 FakeRedisServer::port() const
{
    return m_server.serverPort();
}

void FakeRedisServer::setHandler(Handler handler)
{
    m_handler = std::move(handler);
}

const QList<QList<QByteArray>> &FakeRedisServer::received() const
{
    return m_received;
}

void FakeRedisServer::handleCommand(QTcpSocket *socket, const QList<QByteArray> &args)
{
    m_received.append(args);
    const QByteArray reply = m_handler ? m_handler(args) : defaultReply(args);
    socket->write(reply);
    socket->flush();
}

} // namespace KeyDis::Test
