// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QObject>
#include <QString>

#include <memory>

class QProcess;
class QTcpServer;
class QTcpSocket;
class QTimer;

namespace KeyDis {

class SshTunnel : public QObject
{
    Q_OBJECT

public:
    explicit SshTunnel(QObject *parent = nullptr);
    ~SshTunnel() override;

    void start(const QString &sshHost, quint16 sshPort, const QString &sshUser,
               const QString &keyPath, const QString &targetHost, quint16 targetPort);
    void stop();

    [[nodiscard]] bool isActive() const;
    [[nodiscard]] quint16 localPort() const;

signals:
    void ready(quint16 localPort);
    void failed(const QString &error);

private:
    void probe();

    std::unique_ptr<QProcess> m_process;
    std::unique_ptr<QTcpServer> m_portReserver;
    std::unique_ptr<QTcpSocket> m_probe;
    QTimer *m_timer = nullptr;
    quint16 m_localPort = 0;
    bool m_active = false;
    bool m_probing = false;
    int m_attempts = 0;
};

} // namespace KeyDis
