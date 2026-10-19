// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include "core/net/redisclient.h"

#include <memory>

namespace KeyDis {

class RedisRouter : public RedisClient
{
    Q_OBJECT

public:
    explicit RedisRouter(QObject *parent = nullptr);
    ~RedisRouter() override;

    void setBackend(std::unique_ptr<RedisClient> backend);
    [[nodiscard]] RedisClient *backend() const;

    void connectToHost(const QString &host, quint16 port, bool tls, const QString &username,
                       const QString &password, int db) override;
    void setTlsConfig(const TlsConfig &config) override;
    void disconnectFromHost() override;
    [[nodiscard]] bool isReady() const override;
    void sendCommand(const QList<QByteArray> &args, ReplyCallback callback = {}) override;
    void sendTransaction(const QList<QList<QByteArray>> &commands,
                         ReplyCallback callback) override;

private:
    std::unique_ptr<RedisClient> m_backend;
};

} // namespace KeyDis
