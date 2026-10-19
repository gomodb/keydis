// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include "core/net/redisclient.h"

#include <QAbstractSocket>
#include <QList>
#include <QObject>
#include <QQueue>
#include <QString>

#include <functional>
#include <memory>

namespace KeyDis {

class HiredisReader;

class RedisConnection : public RedisClient
{
    Q_OBJECT

public:
    explicit RedisConnection(QObject *parent = nullptr);
    ~RedisConnection() override;

    void connectToHost(const QString &host, quint16 port, bool tls, const QString &username,
                       const QString &password, int db) override;
    void disconnectFromHost() override;

    [[nodiscard]] bool isReady() const override;

    void sendCommand(const QList<QByteArray> &args, ReplyCallback callback = {}) override;
    void sendTransaction(const QList<QList<QByteArray>> &commands,
                         ReplyCallback callback) override;

private:
    enum class Stage {
        Idle,
        Connecting,
        Hello,
        Auth,
        Select,
        Ready
    };

    void createSocket(bool tls);
    void onTransportReady();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);
    void onDisconnected();
    void startHandshake();
    void startResp2Handshake();
    void completeHandshake();
    void handleReply(const RespValue &reply);
    void pumpPending();
    struct TransactionState;
    void sendTransactionStep(const std::shared_ptr<TransactionState> &state);
    void finishTransaction(const std::shared_ptr<TransactionState> &state, const RespValue &reply,
                           const QString &error);
    bool sendRaw(const QList<QByteArray> &args);
    void setStage(Stage stage, const QString &description);
    void fail(const QString &error);
    void failPending(const QString &error);
    void clearSocket();

    struct Request {
        QList<QByteArray> args;
        ReplyCallback callback;
        bool sent = false;
    };

    QAbstractSocket *m_socket = nullptr;
    std::unique_ptr<HiredisReader> m_reader;
    Stage m_stage = Stage::Idle;
    QString m_host;
    QString m_username;
    QString m_password;
    int m_db = 0;
    bool m_tls = false;
    bool m_userDisconnect = false;
    bool m_authWithUsername = true;
    bool m_resp2 = false;
    bool m_inTransaction = false;
    QQueue<Request> m_pending;
};

} // namespace KeyDis
