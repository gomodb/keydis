// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include "core/resp/respvalue.h"

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>

namespace KeyDis {

class RedisClient : public QObject
{
    Q_OBJECT

public:
    using ReplyCallback = std::function<void(const RespValue &reply, const QString &error)>;

    explicit RedisClient(QObject *parent = nullptr)
        : QObject(parent)
    {
    }
    ~RedisClient() override = default;

    virtual void connectToHost(const QString &host, quint16 port, bool tls,
                               const QString &username, const QString &password, int db) = 0;
    virtual void disconnectFromHost() = 0;
    [[nodiscard]] virtual bool isReady() const = 0;
    virtual void sendCommand(const QList<QByteArray> &args, ReplyCallback callback = {}) = 0;
    virtual void sendTransaction(const QList<QList<QByteArray>> &commands,
                                 ReplyCallback callback) = 0;

signals:
    void stateChanged(const QString &state);
    void errorOccurred(const QString &error);
    void replyReady(const RespValue &reply);
    void pushReceived(const RespValue &reply);
};

} // namespace KeyDis
