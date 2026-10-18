// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "core/conn/sentinelresolver.h"

#include "core/net/redisconnection.h"

#include <QTimer>

namespace KeyDis {

SentinelResolver::SentinelResolver(QObject *parent)
    : QObject(parent)
{
}

SentinelResolver::~SentinelResolver() = default;

void SentinelResolver::resolve(const QString &host, quint16 port, bool tls,
                               const QString &username, const QString &password,
                               const QString &masterName)
{
    if (host.isEmpty() || masterName.isEmpty()) {
        emit failed(tr("Sentinel host and master name are required"));
        return;
    }
    m_masterName = masterName;
    m_done = false;
    m_connection = std::make_unique<RedisConnection>();

    auto *timeout = new QTimer(this);
    timeout->setSingleShot(true);
    timeout->setInterval(10000);
    connect(timeout, &QTimer::timeout, this, [this, timeout] {
        if (m_done)
            return;
        m_done = true;
        emit failed(tr("Sentinel query timed out"));
        timeout->deleteLater();
    });
    timeout->start();

    connect(m_connection.get(), &RedisConnection::errorOccurred, this,
            [this, timeout](const QString &error) {
        if (m_done)
            return;
        m_done = true;
        timeout->stop();
        timeout->deleteLater();
        emit failed(error);
    });
    connect(m_connection.get(), &RedisConnection::stateChanged, this,
            [this, timeout](const QString &state) {
        if (m_done || (state != QLatin1String("Ready")
                       && state != QLatin1String("Ready (RESP2)")))
            return;
        m_connection->sendCommand(
            {QByteArrayLiteral("SENTINEL"), QByteArrayLiteral("get-master-addr-by-name"),
             m_masterName.toUtf8()},
            [this, timeout](const RespValue &reply, const QString &error) {
            if (m_done)
                return;
            m_done = true;
            timeout->stop();
            timeout->deleteLater();
            if (!error.isEmpty() || reply.isError() || reply.type != RespType::Array
                || reply.elements.size() != 2) {
                emit failed(error.isEmpty() ? tr("Sentinel returned an invalid master address")
                                            : error);
                return;
            }
            const QString masterHost = QString::fromUtf8(reply.elements[0].scalar);
            bool ok = false;
            const int masterPort = QString::fromUtf8(reply.elements[1].scalar).toInt(&ok);
            if (masterHost.isEmpty() || !ok || masterPort <= 0 || masterPort > 65535) {
                emit failed(tr("Sentinel returned an invalid master address"));
                return;
            }
            m_connection->disconnectFromHost();
            emit resolved(masterHost, static_cast<quint16>(masterPort));
        });
    });

    m_connection->connectToHost(host, port, tls, username, password, 0);
}

void SentinelResolver::cancel()
{
    m_done = true;
    if (m_connection)
        m_connection->disconnectFromHost();
}

} // namespace KeyDis
