// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "core/net/sshtunnel.h"

#include <QProcess>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

namespace KeyDis {

SshTunnel::SshTunnel(QObject *parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
{
    m_timer->setInterval(150);
    connect(m_timer, &QTimer::timeout, this, &SshTunnel::probe);
}

SshTunnel::~SshTunnel()
{
    stop();
}

void SshTunnel::start(const QString &sshHost, quint16 sshPort, const QString &sshUser,
                      const QString &keyPath, const QString &targetHost, quint16 targetPort)
{
    stop();
    if (sshHost.isEmpty() || targetHost.isEmpty() || targetPort == 0) {
        emit failed(tr("SSH host and target are required"));
        return;
    }

    m_portReserver = std::make_unique<QTcpServer>();
    if (!m_portReserver->listen(QHostAddress::LocalHost, 0)) {
        emit failed(tr("Could not allocate a local tunnel port"));
        return;
    }
    m_localPort = m_portReserver->serverPort();
    m_portReserver->close();
    m_portReserver.reset();

    m_process = std::make_unique<QProcess>();
    QStringList args{
        QStringLiteral("-N"),
        QStringLiteral("-o"), QStringLiteral("ExitOnForwardFailure=yes"),
        QStringLiteral("-o"), QStringLiteral("BatchMode=yes"),
        QStringLiteral("-o"), QStringLiteral("StrictHostKeyChecking=accept-new"),
        QStringLiteral("-L"),
        QStringLiteral("127.0.0.1:%1:%2:%3").arg(m_localPort).arg(targetHost).arg(targetPort),
        QStringLiteral("-p"), QString::number(sshPort),
    };
    if (!keyPath.isEmpty())
        args << QStringLiteral("-i") << keyPath;
    const QString destination = sshUser.isEmpty() ? sshHost
                                                  : sshUser + QLatin1Char('@') + sshHost;
    args << destination;
    m_process->setProgram(QStringLiteral("ssh"));
    m_process->setArguments(args);

    connect(m_process.get(), &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (!m_active)
            emit failed(tr("SSH tunnel failed: %1").arg(m_process->errorString()));
        QTimer::singleShot(0, this, &SshTunnel::stop);
    });
    connect(m_process.get(), qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) {
        if (!m_active)
            emit failed(tr("SSH tunnel closed before it was ready"));
        QTimer::singleShot(0, this, &SshTunnel::stop);
    });

    m_process->start();
    m_attempts = 0;
    m_timer->start();
}

void SshTunnel::probe()
{
    if (m_active || !m_process)
        return;
    if (++m_attempts > 80) {
        emit failed(tr("SSH tunnel timed out"));
        stop();
        return;
    }
    if (m_probing)
        return;
    m_probing = true;
    m_probe = std::make_unique<QTcpSocket>();
    connect(m_probe.get(), &QTcpSocket::connected, this, [this] {
        m_active = true;
        m_probing = false;
        if (m_timer)
            m_timer->stop();
        m_probe.reset();
        emit ready(m_localPort);
    });
    connect(m_probe.get(), &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        m_probing = false;
        m_probe.reset();
    });
    m_probe->connectToHost(QHostAddress::LocalHost, m_localPort);
}

void SshTunnel::stop()
{
    m_active = false;
    m_probing = false;
    if (m_timer)
        m_timer->stop();
    if (m_probe)
        m_probe.reset();
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
        m_process.reset();
    }
    m_portReserver.reset();
    m_localPort = 0;
}

bool SshTunnel::isActive() const
{
    return m_active;
}

quint16 SshTunnel::localPort() const
{
    return m_localPort;
}

} // namespace KeyDis
