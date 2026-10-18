// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QObject>
#include <QString>

#include <memory>

namespace KeyDis {

class RedisConnection;

class SentinelResolver : public QObject
{
    Q_OBJECT

public:
    explicit SentinelResolver(QObject *parent = nullptr);
    ~SentinelResolver() override;

    void resolve(const QString &host, quint16 port, bool tls, const QString &username,
                 const QString &password, const QString &masterName);
    void cancel();

signals:
    void resolved(const QString &host, quint16 port);
    void failed(const QString &error);

private:
    std::unique_ptr<RedisConnection> m_connection;
    QString m_masterName;
    bool m_done = false;
};

} // namespace KeyDis
