// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "ui/dialogs/profiledialog.h"

#include "core/net/redisconnection.h"

#include <QCheckBox>
#include <QColor>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace KeyDis {

ProfileDialog::ProfileDialog(const ConnectionProfile &profile, QWidget *parent)
    : QDialog(parent)
    , m_profile(profile)
{
    setWindowTitle(profile.name.isEmpty() ? tr("New connection") : tr("Edit connection"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);

    auto *connectionTab = new QWidget(m_tabs);
    auto *form = new QFormLayout(connectionTab);
    m_nameEdit = new QLineEdit(profile.name, connectionTab);
    form->addRow(tr("Name"), m_nameEdit);
    m_groupEdit = new QLineEdit(profile.group, connectionTab);
    form->addRow(tr("Group"), m_groupEdit);
    m_colorEdit = new QLineEdit(profile.color, connectionTab);
    m_colorEdit->setPlaceholderText(tr("Optional color, e.g. #3daee9"));
    form->addRow(tr("Color"), m_colorEdit);
    m_hostEdit = new QLineEdit(profile.host, connectionTab);
    form->addRow(tr("Host"), m_hostEdit);
    m_portSpin = new QSpinBox(connectionTab);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(profile.port);
    form->addRow(tr("Port"), m_portSpin);
    m_tlsCheck = new QCheckBox(tr("Use TLS"), connectionTab);
    m_tlsCheck->setChecked(profile.tls);
    form->addRow(QString(), m_tlsCheck);
    m_usernameEdit = new QLineEdit(profile.username, connectionTab);
    form->addRow(tr("ACL user"), m_usernameEdit);
    m_passwordEdit = new QLineEdit(profile.password, connectionTab);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(tr("Leave blank to use KWallet"));
    form->addRow(tr("Password"), m_passwordEdit);
    m_dbSpin = new QSpinBox(connectionTab);
    m_dbSpin->setRange(0, 15);
    m_dbSpin->setValue(profile.db);
    form->addRow(tr("Database"), m_dbSpin);
    m_readOnlyCheck = new QCheckBox(tr("Read-only"), connectionTab);
    m_readOnlyCheck->setChecked(profile.readOnly);
    form->addRow(QString(), m_readOnlyCheck);
    m_tabs->addTab(connectionTab, tr("Connection"));

    auto *advancedTab = new QWidget(m_tabs);
    auto *advancedForm = new QFormLayout(advancedTab);
    m_clusterCheck = new QCheckBox(tr("Cluster mode"), advancedTab);
    m_clusterCheck->setChecked(profile.cluster);
    advancedForm->addRow(QString(), m_clusterCheck);
    m_sentinelCheck = new QCheckBox(tr("Use Sentinel"), advancedTab);
    m_sentinelCheck->setChecked(profile.sentinel);
    advancedForm->addRow(QString(), m_sentinelCheck);
    m_sentinelMasterEdit = new QLineEdit(profile.sentinelMaster, advancedTab);
    m_sentinelMasterEdit->setPlaceholderText(tr("Master name, e.g. mymaster"));
    advancedForm->addRow(tr("Sentinel master"), m_sentinelMasterEdit);
    m_sentinelHostEdit = new QLineEdit(profile.sentinelHost, advancedTab);
    advancedForm->addRow(tr("Sentinel host"), m_sentinelHostEdit);
    m_sentinelPortSpin = new QSpinBox(advancedTab);
    m_sentinelPortSpin->setRange(1, 65535);
    m_sentinelPortSpin->setValue(profile.sentinelPort);
    advancedForm->addRow(tr("Sentinel port"), m_sentinelPortSpin);
    m_sentinelTlsCheck = new QCheckBox(tr("Sentinel TLS"), advancedTab);
    m_sentinelTlsCheck->setChecked(profile.sentinelTls);
    advancedForm->addRow(QString(), m_sentinelTlsCheck);
    m_tlsCaEdit = new QLineEdit(profile.tlsCaFile, advancedTab);
    m_tlsCaEdit->setPlaceholderText(tr("Optional CA certificate path"));
    advancedForm->addRow(tr("TLS CA"), m_tlsCaEdit);
    m_tlsCertEdit = new QLineEdit(profile.tlsCertFile, advancedTab);
    m_tlsCertEdit->setPlaceholderText(tr("Optional client certificate path"));
    advancedForm->addRow(tr("TLS cert"), m_tlsCertEdit);
    m_tlsKeyEdit = new QLineEdit(profile.tlsKeyFile, advancedTab);
    m_tlsKeyEdit->setPlaceholderText(tr("Optional client private key path"));
    advancedForm->addRow(tr("TLS key"), m_tlsKeyEdit);
    m_tlsVerifyCheck = new QCheckBox(tr("Verify TLS certificate"), advancedTab);
    m_tlsVerifyCheck->setChecked(profile.tlsVerifyPeer);
    advancedForm->addRow(QString(), m_tlsVerifyCheck);
    m_sshCheck = new QCheckBox(tr("Use SSH tunnel"), advancedTab);
    m_sshCheck->setChecked(profile.sshTunnel);
    advancedForm->addRow(QString(), m_sshCheck);
    m_sshHostEdit = new QLineEdit(profile.sshHost, advancedTab);
    m_sshHostEdit->setPlaceholderText(tr("SSH jump host"));
    advancedForm->addRow(tr("SSH host"), m_sshHostEdit);
    m_sshPortSpin = new QSpinBox(advancedTab);
    m_sshPortSpin->setRange(1, 65535);
    m_sshPortSpin->setValue(profile.sshPort);
    advancedForm->addRow(tr("SSH port"), m_sshPortSpin);
    m_sshUserEdit = new QLineEdit(profile.sshUser, advancedTab);
    advancedForm->addRow(tr("SSH user"), m_sshUserEdit);
    m_sshKeyEdit = new QLineEdit(profile.sshKeyPath, advancedTab);
    m_sshKeyEdit->setPlaceholderText(tr("Optional private key path"));
    advancedForm->addRow(tr("SSH key"), m_sshKeyEdit);
    m_sshPasswordEdit = new QLineEdit(profile.sshPassword, advancedTab);
    m_sshPasswordEdit->setEchoMode(QLineEdit::Password);
    m_sshPasswordEdit->setPlaceholderText(tr("Optional; stored in KWallet"));
    advancedForm->addRow(tr("SSH password"), m_sshPasswordEdit);
    advancedForm->addRow(QString(),
                         new QLabel(tr("SSH supports keys/agent or password via askpass."),
                                    advancedTab));
    advancedForm->addRow(QString(),
                         new QLabel(tr("Passwords are stored separately in KWallet."), advancedTab));
    m_tabs->addTab(advancedTab, tr("Advanced"));

    layout->addWidget(m_tabs, 1);
    auto *testRow = new QHBoxLayout;
    auto *testButton = new QPushButton(tr("Test connection"), this);
    testButton->setObjectName(QStringLiteral("testConnectionButton"));
    testRow->addWidget(testButton);
    m_testStatus = new QLabel(this);
    m_testStatus->setObjectName(QStringLiteral("testConnectionStatus"));
    m_testStatus->setWordWrap(true);
    m_testStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    testRow->addWidget(m_testStatus, 1);
    layout->addLayout(testRow);
    connect(testButton, &QPushButton::clicked, this, &ProfileDialog::testConnection);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &ProfileDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &ProfileDialog::reject);
}

ConnectionProfile ProfileDialog::profile() const
{
    return m_profile;
}

void ProfileDialog::testConnection()
{
    if (m_testing)
        return;
    if (m_sshCheck->isChecked() || m_sentinelCheck->isChecked()) {
        m_testStatus->setText(tr("Test connection is available for direct connections only."));
        return;
    }
    if (m_hostEdit->text().trimmed().isEmpty()) {
        m_testStatus->setText(tr("Host must not be empty."));
        return;
    }
    m_testing = true;
    m_testStatus->setText(tr("Testing..."));
    auto *connection = new RedisConnection(this);
    QPointer<RedisConnection> connectionGuard(connection);
    QPointer<ProfileDialog> guard(this);
    QTimer::singleShot(6000, this, [guard, connectionGuard] {
        if (!guard || !connectionGuard)
            return;
        connectionGuard->disconnectFromHost();
        connectionGuard->deleteLater();
        if (guard->m_testing) {
            guard->m_testing = false;
            guard->m_testStatus->setText(tr("Connection test timed out."));
        }
    });
    connect(connection, &RedisClient::stateChanged, this,
            [guard, connectionGuard](const QString &state) {
        if (!guard || !connectionGuard
            || (state != QLatin1String("Ready") && state != QLatin1String("Ready (RESP2)")))
            return;
        guard->m_testing = false;
        guard->m_testStatus->setText(tr("Connection successful."));
        connectionGuard->disconnectFromHost();
        connectionGuard->deleteLater();
    });
    connect(connection, &RedisClient::errorOccurred, this,
            [guard, connectionGuard](const QString &error) {
        if (!guard || !connectionGuard)
            return;
        guard->m_testing = false;
        guard->m_testStatus->setText(error);
        connectionGuard->deleteLater();
    });
    TlsConfig tlsConfig;
    tlsConfig.enabled = m_tlsCheck->isChecked();
    tlsConfig.caFile = m_tlsCaEdit->text().trimmed();
    tlsConfig.certFile = m_tlsCertEdit->text().trimmed();
    tlsConfig.keyFile = m_tlsKeyEdit->text().trimmed();
    tlsConfig.verifyPeer = m_tlsVerifyCheck->isChecked();
    connection->setTlsConfig(tlsConfig);
    connection->connectToHost(m_hostEdit->text().trimmed(),
                              static_cast<quint16>(m_portSpin->value()),
                              m_tlsCheck->isChecked(), m_usernameEdit->text(),
                              m_passwordEdit->text(), m_dbSpin->value());
}

void ProfileDialog::accept()
{
    if (m_nameEdit->text().trimmed().isEmpty() || m_hostEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Invalid connection"),
                             tr("Name and host must not be empty."));
        return;
    }
    m_profile.name = m_nameEdit->text().trimmed();
    m_profile.group = m_groupEdit->text().trimmed().isEmpty() ? tr("Default") : m_groupEdit->text().trimmed();
    m_profile.color = m_colorEdit->text().trimmed();
    if (!m_profile.color.isEmpty() && !QColor(m_profile.color).isValid()) {
        QMessageBox::warning(this, tr("Invalid connection"),
                             tr("Color must be a valid color name or hex value."));
        return;
    }
    m_profile.host = m_hostEdit->text().trimmed();
    m_profile.port = static_cast<quint16>(m_portSpin->value());
    m_profile.tls = m_tlsCheck->isChecked();
    m_profile.tlsCaFile = m_tlsCaEdit->text().trimmed();
    m_profile.tlsCertFile = m_tlsCertEdit->text().trimmed();
    m_profile.tlsKeyFile = m_tlsKeyEdit->text().trimmed();
    m_profile.tlsVerifyPeer = m_tlsVerifyCheck->isChecked();
    m_profile.username = m_usernameEdit->text();
    m_profile.password = m_passwordEdit->text();
    m_profile.db = m_dbSpin->value();
    m_profile.readOnly = m_readOnlyCheck->isChecked();
    m_profile.cluster = m_clusterCheck->isChecked();
    m_profile.sentinel = m_sentinelCheck->isChecked();
    m_profile.sentinelMaster = m_sentinelMasterEdit->text().trimmed();
    m_profile.sentinelHost = m_sentinelHostEdit->text().trimmed();
    m_profile.sentinelPort = static_cast<quint16>(m_sentinelPortSpin->value());
    m_profile.sentinelTls = m_sentinelTlsCheck->isChecked();
    m_profile.sshTunnel = m_sshCheck->isChecked();
    m_profile.sshHost = m_sshHostEdit->text().trimmed();
    m_profile.sshPort = static_cast<quint16>(m_sshPortSpin->value());
    m_profile.sshUser = m_sshUserEdit->text().trimmed();
    m_profile.sshKeyPath = m_sshKeyEdit->text().trimmed();
    m_profile.sshPassword = m_sshPasswordEdit->text();
    if (m_profile.sshTunnel && m_profile.sshHost.isEmpty()) {
        QMessageBox::warning(this, tr("Invalid SSH configuration"),
                             tr("SSH host must not be empty."));
        return;
    }
    if (m_profile.sshTunnel && m_profile.sentinel) {
        QMessageBox::warning(this, tr("Invalid connection"),
                             tr("SSH and Sentinel cannot be combined yet."));
        return;
    }
    if (m_profile.cluster && (m_profile.sentinel || m_profile.sshTunnel)) {
        QMessageBox::warning(this, tr("Invalid connection"),
                             tr("Cluster cannot be combined with Sentinel or SSH yet."));
        return;
    }
    if (m_profile.sentinel
        && (m_profile.sentinelMaster.isEmpty() || m_profile.sentinelHost.isEmpty())) {
        QMessageBox::warning(this, tr("Invalid Sentinel configuration"),
                             tr("Sentinel master and host must not be empty."));
        return;
    }
    QDialog::accept();
}

} // namespace KeyDis
