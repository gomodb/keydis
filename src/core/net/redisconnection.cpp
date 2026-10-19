// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "core/net/redisconnection.h"

#include "core/resp/commandencoder.h"
#include "core/resp/hiredisreader.h"

#include <QSslError>
#include <QSslSocket>
#include <QTcpSocket>

#include <utility>

namespace KeyDis {

RedisConnection::RedisConnection(QObject *parent)
    : RedisClient(parent)
{
}

RedisConnection::~RedisConnection()
{
    clearSocket();
}

void RedisConnection::connectToHost(const QString &host, quint16 port, bool tls,
                                    const QString &username, const QString &password, int db)
{
    m_host = host;
    m_username = username;
    m_password = password;
    m_db = db;
    m_tls = tls;
    m_userDisconnect = false;
    m_inTransaction = false;
    m_authWithUsername = !m_username.isEmpty();
    m_resp2 = false;

    clearSocket();
    failPending(QStringLiteral("Reconnecting"));
    m_pending.clear();
    m_reader.reset();

    createSocket(tls);
    setStage(Stage::Connecting, QStringLiteral("Connecting to %1:%2...").arg(host).arg(port));
    m_socket->connectToHost(host, port);
}

void RedisConnection::createSocket(bool tls)
{
    if (tls) {
        auto *socket = new QSslSocket(this);
        socket->setPeerVerifyMode(QSslSocket::VerifyPeer);
        socket->setPeerVerifyName(m_host);
        connect(socket, &QSslSocket::encrypted, this, &RedisConnection::onTransportReady);
        connect(socket, &QSslSocket::sslErrors, this, [this](const QList<QSslError> &errors) {
            if (m_stage == Stage::Idle)
                return;
            const QString detail =
                errors.isEmpty() ? QStringLiteral("unknown TLS error") : errors.first().errorString();
            fail(QStringLiteral("TLS verification failed: %1").arg(detail));
        });
        m_socket = socket;
    } else {
        auto *socket = new QTcpSocket(this);
        connect(socket, &QTcpSocket::connected, this, &RedisConnection::onTransportReady);
        m_socket = socket;
    }

    connect(m_socket, &QAbstractSocket::readyRead, this, &RedisConnection::onReadyRead);
    connect(m_socket, &QAbstractSocket::errorOccurred, this, &RedisConnection::onSocketError);
    connect(m_socket, &QAbstractSocket::disconnected, this, &RedisConnection::onDisconnected);
}

void RedisConnection::disconnectFromHost()
{
    m_userDisconnect = true;
    m_inTransaction = false;
    m_reader.reset();
    if (m_stage != Stage::Idle)
        setStage(Stage::Idle, QStringLiteral("Disconnected"));
    failPending(QStringLiteral("Disconnected"));
    clearSocket();
}

bool RedisConnection::isReady() const
{
    return m_stage == Stage::Ready;
}

void RedisConnection::onTransportReady()
{
    if (m_stage != Stage::Connecting)
        return;

    m_reader = std::make_unique<HiredisReader>();
    startHandshake();
}

void RedisConnection::startHandshake()
{
    setStage(Stage::Hello, QStringLiteral("Negotiating RESP3..."));
    QList<QByteArray> hello{QByteArrayLiteral("HELLO"), QByteArrayLiteral("3")};
    if (!m_password.isEmpty()) {
        hello.append(QByteArrayLiteral("AUTH"));
        hello.append((m_username.isEmpty() ? QStringLiteral("default") : m_username).toUtf8());
        hello.append(m_password.toUtf8());
    }
    if (!sendRaw(hello))
        fail(QStringLiteral("Failed to send HELLO: %1").arg(m_socket ? m_socket->errorString() : QString()));
}

void RedisConnection::startResp2Handshake()
{
    m_resp2 = true;
    setStage(Stage::Auth, QStringLiteral("Falling back to RESP2..."));
    if (m_password.isEmpty()) {
        completeHandshake();
        return;
    }

    QList<QByteArray> auth{QByteArrayLiteral("AUTH")};
    if (m_authWithUsername && !m_username.isEmpty())
        auth.append(m_username.toUtf8());
    auth.append(m_password.toUtf8());
    if (!sendRaw(auth))
        fail(QStringLiteral("Failed to send AUTH"));
}

void RedisConnection::completeHandshake()
{
    if (m_db > 0) {
        setStage(Stage::Select, QStringLiteral("Selecting database %1...").arg(m_db));
        if (!sendRaw({QByteArrayLiteral("SELECT"), QByteArray::number(m_db)}))
            fail(QStringLiteral("Failed to send SELECT"));
        return;
    }
    setStage(Stage::Ready, m_resp2 ? QStringLiteral("Ready (RESP2)")
                                   : QStringLiteral("Ready"));
    pumpPending();
}

void RedisConnection::handleReply(const RespValue &reply)
{
    if (m_stage == Stage::Hello) {
        if (reply.isError()) {
            startResp2Handshake();
            return;
        }
        completeHandshake();
        return;
    }

    if (m_stage == Stage::Auth) {
        if (reply.isError()) {
            if (m_authWithUsername && !m_username.isEmpty()) {
                m_authWithUsername = false;
                startResp2Handshake();
                return;
            }
            fail(QStringLiteral("AUTH failed: %1").arg(QString::fromUtf8(reply.scalar)));
            return;
        }
        completeHandshake();
        return;
    }

    if (m_stage == Stage::Select) {
        if (reply.isError()) {
            fail(QStringLiteral("SELECT failed: %1").arg(QString::fromUtf8(reply.scalar)));
            return;
        }
        setStage(Stage::Ready, m_resp2 ? QStringLiteral("Ready (RESP2)")
                                       : QStringLiteral("Ready"));
        pumpPending();
        return;
    }

    if (m_stage != Stage::Ready)
        return;

    if (reply.type == RespType::Push) {
        emit pushReceived(reply);
        return;
    }

    if (!m_pending.isEmpty()) {
        const Request request = m_pending.dequeue();
        if (request.callback) {
            request.callback(reply, QString());
            return;
        }
    }
    emit replyReady(reply);
}

void RedisConnection::onReadyRead()
{
    if (!m_socket || !m_reader)
        return;

    const QByteArray data = m_socket->readAll();
    if (data.isEmpty())
        return;

    QString error;
    if (!m_reader->feed(data.constData(), data.size(), &error)) {
        fail(error);
        return;
    }

    while (m_stage != Stage::Idle) {
        RespValue reply;
        const HiredisReader::ReadResult result = m_reader->read(&reply, &error);
        if (result == HiredisReader::ReadResult::NeedMoreData)
            break;
        if (result == HiredisReader::ReadResult::Error) {
            fail(error);
            break;
        }
        handleReply(reply);
    }
}

void RedisConnection::onSocketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error)
    if (m_stage == Stage::Idle || m_userDisconnect)
        return;
    fail(m_socket ? m_socket->errorString() : QStringLiteral("socket error"));
}

void RedisConnection::onDisconnected()
{
    if (m_stage == Stage::Idle)
        return;
    const bool wasUser = m_userDisconnect;
    m_userDisconnect = false;
    m_reader.reset();
    setStage(Stage::Idle, QStringLiteral("Disconnected"));
    failPending(wasUser ? QStringLiteral("Disconnected")
                        : QStringLiteral("Connection closed by the server"));
    if (!wasUser)
        emit errorOccurred(QStringLiteral("Connection closed by the server"));
}

void RedisConnection::pumpPending()
{
    if (m_stage != Stage::Ready)
        return;
    for (Request &request : m_pending) {
        if (request.sent)
            continue;
        request.sent = true;
        if (!sendRaw(request.args)) {
            fail(QStringLiteral("Failed to write to socket: %1")
                     .arg(m_socket ? m_socket->errorString() : QString()));
            return;
        }
    }
}

void RedisConnection::sendCommand(const QList<QByteArray> &args, ReplyCallback callback)
{
    if (args.isEmpty())
        return;
    if (m_stage == Stage::Idle) {
        const QString error = QStringLiteral("Not connected");
        if (callback)
            callback(RespValue{}, error);
        emit errorOccurred(error);
        return;
    }

    Request request{args, std::move(callback), false};
    m_pending.enqueue(std::move(request));
    if (m_stage == Stage::Ready) {
        m_pending.last().sent = true;
        if (!sendRaw(args))
            fail(QStringLiteral("Failed to write to socket: %1")
                     .arg(m_socket ? m_socket->errorString() : QString()));
    }
}

struct RedisConnection::TransactionState {
    QList<QList<QByteArray>> commands;
    ReplyCallback callback;
    int index = 0;
};

void RedisConnection::sendTransaction(const QList<QList<QByteArray>> &commands,
                                      ReplyCallback callback)
{
    if (commands.isEmpty()) {
        RespValue empty;
        empty.type = RespType::Array;
        if (callback)
            callback(empty, {});
        return;
    }
    if (m_inTransaction) {
        if (callback)
            callback(RespValue{}, tr("A transaction is already in progress"));
        return;
    }
    m_inTransaction = true;
    auto state = std::make_shared<TransactionState>();
    state->commands = commands;
    state->callback = std::move(callback);

    sendCommand({QByteArrayLiteral("MULTI")}, [this, state](const RespValue &reply,
                                                             const QString &error) {
        if (!error.isEmpty() || reply.isError()) {
            m_inTransaction = false;
            if (state->callback)
                state->callback(reply, error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
            return;
        }
        sendTransactionStep(state);
    });
}

void RedisConnection::sendTransactionStep(const std::shared_ptr<TransactionState> &state)
{
    if (!state)
        return;
    if (state->index >= state->commands.size()) {
        sendCommand({QByteArrayLiteral("EXEC")},
                    [this, state](const RespValue &reply, const QString &error) {
            if (!error.isEmpty()) {
                finishTransaction(state, reply, error);
                return;
            }
            if (reply.isError()) {
                finishTransaction(state, reply, QString::fromUtf8(reply.scalar));
                return;
            }
            if (reply.isNull()) {
                finishTransaction(state, reply, tr("Transaction aborted"));
                return;
            }
            finishTransaction(state, reply, {});
        });
        return;
    }

    const QList<QByteArray> command = state->commands.at(state->index);
    sendCommand(command, [this, state](const RespValue &reply, const QString &error) {
        if (!error.isEmpty() || reply.isError()) {
            const QString message = error.isEmpty() ? QString::fromUtf8(reply.scalar) : error;
            sendCommand({QByteArrayLiteral("DISCARD")},
                        [this, state, message](const RespValue &, const QString &) {
                finishTransaction(state, RespValue{}, message);
            });
            return;
        }
        ++state->index;
        sendTransactionStep(state);
    });
}

void RedisConnection::finishTransaction(const std::shared_ptr<TransactionState> &state,
                                        const RespValue &reply, const QString &error)
{
    m_inTransaction = false;
    if (state && state->callback)
        state->callback(reply, error);
}

bool RedisConnection::sendRaw(const QList<QByteArray> &args)
{
    if (!m_socket || m_stage == Stage::Idle)
        return false;
    const QByteArray payload = CommandEncoder::encode(args);
    return m_socket->write(payload) >= 0;
}

void RedisConnection::setStage(Stage stage, const QString &description)
{
    m_stage = stage;
    emit stateChanged(description);
}

void RedisConnection::fail(const QString &error)
{
    if (m_stage == Stage::Idle)
        return;
    m_reader.reset();
    m_inTransaction = false;
    setStage(Stage::Idle, QStringLiteral("Disconnected"));
    failPending(error);
    emit errorOccurred(error);
    clearSocket();
}

void RedisConnection::failPending(const QString &error)
{
    while (!m_pending.isEmpty()) {
        const Request request = m_pending.dequeue();
        if (request.callback)
            request.callback(RespValue{}, error);
    }
}

void RedisConnection::clearSocket()
{
    if (!m_socket)
        return;
    QObject::disconnect(m_socket, nullptr, this, nullptr);
    m_socket->abort();
    m_socket->deleteLater();
    m_socket = nullptr;
}

} // namespace KeyDis
