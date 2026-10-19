// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QJsonObject>
#include <QString>

namespace KeyDis {

struct ConnectionProfile {
    QString id;
    QString name = QStringLiteral("default");
    QString group = QStringLiteral("Default");
    QString color;
    QString host = QStringLiteral("127.0.0.1");
    quint16 port = 6379;
    bool tls = false;
    QString username;
    // Transient only: never serialized to connections.json. The dialog may
    // populate it for the current session; KWallet is the durable store.
    QString password;
    int db = 0;
    bool readOnly = false;
    bool cluster = false;
    bool sentinel = false;
    QString sentinelMaster;
    QString sentinelHost;
    quint16 sentinelPort = 26379;
    bool sentinelTls = false;
    bool sshTunnel = false;
    QString sshHost;
    quint16 sshPort = 22;
    QString sshUser;
    QString sshKeyPath;
    QString sshPassword; // transient; KWallet is the durable store

    [[nodiscard]] QJsonObject toJson() const;
    static ConnectionProfile fromJson(const QJsonObject &object);
};

} // namespace KeyDis
