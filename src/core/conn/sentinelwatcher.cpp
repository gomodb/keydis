// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "core/conn/sentinelwatcher.h"

#include "core/net/redisconnection.h"

#include <QTimer>

namespace KeyDis {

SentinelWatcher::SentinelWatcher(QObject *parent)
    : QObject(parent)
    , m_retry(new QTimer(this))
{
    m_retry->setSingleShot(true);
    m_retry->setInterval(2000);
    connect(m_retry, &QTimer::timeout, this, &SentinelWatcher::connectWatcher);
}

SentinelWatcher::~SentinelWatcher()
{
    stop();
}

void SentinelWatcher::start(const QString &host, quint16 port, bool tls,
                            const QString &username, const QString &password,
                            const QString &masterName)
{
    stop();
    m_host = host;
    m_port = port;
    m_tls = tls;
    m_username = username;
    m_password = password;
    m_masterName = masterName;
    m_active = true;
    connectWatcher();
}

void SentinelWatcher::connectWatcher()
{
    if (!m_active)
        return;
    m_connection = std::make_unique<RedisConnection>();
    connect(m_connection.get(), &RedisClient::stateChanged, this, [this](const QString &state) {
        if (!m_active)
            return;
        if (state != QLatin1String("Ready") && state != QLatin1String("Ready (RESP2)"))
            return;
        m_connection->sendCommand({QByteArrayLiteral("SUBSCRIBE"),
                                   QByteArrayLiteral("+switch-master")});
    });
    connect(m_connection.get(), &RedisClient::replyReady, this,
            [this](const RespValue &reply) { handleMessage(reply); });
    connect(m_connection.get(), &RedisClient::pushReceived, this,
            [this](const RespValue &reply) { handleMessage(reply); });
    connect(m_connection.get(), &RedisClient::errorOccurred, this,
            [this](const QString &error) {
        if (!m_active)
            return;
        emit errorOccurred(error);
        m_retry->start();
    });
    m_connection->connectToHost(m_host, m_port, m_tls, m_username, m_password, 0);
}

bool SentinelWatcher::parseSwitchMaster(const RespValue &reply, const QString &masterName,
                                        QString *host, quint16 *port)
{
    QVector<QByteArray> parts;
    if (reply.type == RespType::Array || reply.type == RespType::Push) {
        for (const RespValue &element : reply.elements)
            parts.append(element.scalar);
    }
    if (parts.size() < 3 || parts.at(1) != QByteArrayLiteral("+switch-master"))
        return false;
    const QList<QByteArray> payload = parts.at(2).split(' ');
    if (payload.size() < 3 || QString::fromUtf8(payload.at(0)) != masterName)
        return false;
    bool ok = false;
    const int value = payload.at(2).toInt(&ok);
    if (!ok || value <= 0 || value > 65535)
        return false;
    if (host)
        *host = QString::fromUtf8(payload.at(1));
    if (port)
        *port = static_cast<quint16>(value);
    return true;
}

void SentinelWatcher::handleMessage(const RespValue &reply)
{
    QString host;
    quint16 port = 0;
    if (!parseSwitchMaster(reply, m_masterName, &host, &port))
        return;
    emit masterChanged(host, port);
}

void SentinelWatcher::stop()
{
    m_active = false;
    if (m_retry)
        m_retry->stop();
    if (m_connection) {
        m_connection->disconnect(this);
        m_connection->disconnectFromHost();
        m_connection.reset();
    }
}

} // namespace KeyDis
