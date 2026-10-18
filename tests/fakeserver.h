// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QTcpServer>
#include <QVector>

#include <functional>

class QTcpSocket;

namespace KeyDis::Test {

class FakeRedisServer : public QObject
{
    Q_OBJECT

public:
    using Handler = std::function<QByteArray(const QList<QByteArray> &)>;

    explicit FakeRedisServer(QObject *parent = nullptr);

    bool start();
    void stop();
    [[nodiscard]] quint16 port() const;
    void setHandler(Handler handler);
    [[nodiscard]] const QList<QList<QByteArray>> &received() const;

private:
    void handleCommand(QTcpSocket *socket, const QList<QByteArray> &args);

    QTcpServer m_server;
    QList<QList<QByteArray>> m_received;
    Handler m_handler;
    QByteArray m_input;
};

} // namespace KeyDis::Test
