// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include "core/resp/respvalue.h"

#include <QAbstractSocket>
#include <QList>
#include <QObject>
#include <QQueue>
#include <QString>

#include <functional>
#include <memory>

namespace KeyDis {

class HiredisReader;

class RedisConnection : public QObject
{
    Q_OBJECT

public:
    explicit RedisConnection(QObject *parent = nullptr);
    ~RedisConnection() override;

    void connectToHost(const QString &host, quint16 port, bool tls, const QString &username,
                       const QString &password, int db);
    void disconnectFromHost();

    [[nodiscard]] bool isReady() const;

    using ReplyCallback = std::function<void(const RespValue &reply, const QString &error)>;

    void sendCommand(const QList<QByteArray> &args, ReplyCallback callback = {});

signals:
    void stateChanged(const QString &state);
    void errorOccurred(const QString &error);
    void replyReady(const RespValue &reply);
    void pushReceived(const RespValue &reply);

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
    QQueue<Request> m_pending;
};

} // namespace KeyDis
