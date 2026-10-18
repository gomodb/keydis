// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include "core/net/redisclient.h"

#include <QObject>
#include <QString>

#include <memory>

class QTimer;

namespace KeyDis {

class RedisConnection;

class SentinelWatcher : public QObject
{
    Q_OBJECT

public:
    explicit SentinelWatcher(QObject *parent = nullptr);
    ~SentinelWatcher() override;

    void start(const QString &host, quint16 port, bool tls, const QString &username,
               const QString &password, const QString &masterName);
    void stop();

    [[nodiscard]] static bool parseSwitchMaster(const RespValue &reply,
                                                const QString &masterName,
                                                QString *host, quint16 *port);

signals:
    void masterChanged(const QString &host, quint16 port);
    void errorOccurred(const QString &error);

private:
    void connectWatcher();
    void handleMessage(const RespValue &reply);

    QString m_host;
    quint16 m_port = 0;
    bool m_tls = false;
    QString m_username;
    QString m_password;
    QString m_masterName;
    std::unique_ptr<RedisConnection> m_connection;
    QTimer *m_retry = nullptr;
    bool m_active = false;
};

} // namespace KeyDis
