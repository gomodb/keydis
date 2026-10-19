// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "core/net/redisrouter.h"

namespace KeyDis {

RedisRouter::RedisRouter(QObject *parent)
    : RedisClient(parent)
{
}

RedisRouter::~RedisRouter() = default;

void RedisRouter::setBackend(std::unique_ptr<RedisClient> backend)
{
    if (m_backend) {
        m_backend->disconnectFromHost();
        m_backend->disconnect(this);
        m_backend.reset();
    }
    if (!backend)
        return;
    m_backend = std::move(backend);
    connect(m_backend.get(), &RedisClient::stateChanged, this, &RedisClient::stateChanged);
    connect(m_backend.get(), &RedisClient::errorOccurred, this, &RedisClient::errorOccurred);
    connect(m_backend.get(), &RedisClient::replyReady, this, &RedisClient::replyReady);
    connect(m_backend.get(), &RedisClient::pushReceived, this, &RedisClient::pushReceived);
}

RedisClient *RedisRouter::backend() const
{
    return m_backend.get();
}

void RedisRouter::connectToHost(const QString &host, quint16 port, bool tls,
                                const QString &username, const QString &password, int db)
{
    if (m_backend)
        m_backend->connectToHost(host, port, tls, username, password, db);
    else
        emit errorOccurred(tr("No connection backend"));
}

void RedisRouter::setTlsConfig(const TlsConfig &config)
{
    if (m_backend)
        m_backend->setTlsConfig(config);
}

void RedisRouter::connectToUnixSocket(const QString &path, const QString &username,
                                      const QString &password, int db)
{
    if (m_backend)
        m_backend->connectToUnixSocket(path, username, password, db);
    else
        emit errorOccurred(tr("No connection backend"));
}

void RedisRouter::disconnectFromHost()
{
    if (m_backend)
        m_backend->disconnectFromHost();
}

bool RedisRouter::isReady() const
{
    return m_backend && m_backend->isReady();
}

void RedisRouter::sendTransaction(const QList<QList<QByteArray>> &commands,
                                  ReplyCallback callback)
{
    if (!m_backend) {
        if (callback)
            callback(RespValue{}, tr("Not connected"));
        return;
    }
    m_backend->sendTransaction(commands, std::move(callback));
}

void RedisRouter::sendCommand(const QList<QByteArray> &args, ReplyCallback callback)
{
    if (!m_backend) {
        if (callback)
            callback(RespValue{}, tr("Not connected"));
        return;
    }
    m_backend->sendCommand(args, std::move(callback));
}

} // namespace KeyDis
