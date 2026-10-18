// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "ui/dialogs/profiledialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSpinBox>
#include <QVBoxLayout>

namespace KeyDis {

ProfileDialog::ProfileDialog(const ConnectionProfile &profile, QWidget *parent)
    : QDialog(parent)
    , m_profile(profile)
{
    setWindowTitle(profile.name.isEmpty() ? tr("New connection") : tr("Edit connection"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;

    m_nameEdit = new QLineEdit(profile.name, this);
    form->addRow(tr("Name"), m_nameEdit);
    m_groupEdit = new QLineEdit(profile.group, this);
    form->addRow(tr("Group"), m_groupEdit);
    m_hostEdit = new QLineEdit(profile.host, this);
    form->addRow(tr("Host"), m_hostEdit);
    m_portSpin = new QSpinBox(this);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(profile.port);
    form->addRow(tr("Port"), m_portSpin);
    m_tlsCheck = new QCheckBox(tr("Use TLS"), this);
    m_tlsCheck->setChecked(profile.tls);
    form->addRow(QString(), m_tlsCheck);
    m_usernameEdit = new QLineEdit(profile.username, this);
    form->addRow(tr("ACL user"), m_usernameEdit);
    m_dbSpin = new QSpinBox(this);
    m_dbSpin->setRange(0, 15);
    m_dbSpin->setValue(profile.db);
    form->addRow(tr("Database"), m_dbSpin);
    m_readOnlyCheck = new QCheckBox(tr("Read-only"), this);
    m_readOnlyCheck->setChecked(profile.readOnly);
    form->addRow(QString(), m_readOnlyCheck);
    m_sentinelCheck = new QCheckBox(tr("Use Sentinel"), this);
    m_sentinelCheck->setChecked(profile.sentinel);
    form->addRow(QString(), m_sentinelCheck);
    m_sentinelMasterEdit = new QLineEdit(profile.sentinelMaster, this);
    m_sentinelMasterEdit->setPlaceholderText(tr("Master name, e.g. mymaster"));
    form->addRow(tr("Sentinel master"), m_sentinelMasterEdit);
    m_sentinelHostEdit = new QLineEdit(profile.sentinelHost, this);
    form->addRow(tr("Sentinel host"), m_sentinelHostEdit);
    m_sentinelPortSpin = new QSpinBox(this);
    m_sentinelPortSpin->setRange(1, 65535);
    m_sentinelPortSpin->setValue(profile.sentinelPort);
    form->addRow(tr("Sentinel port"), m_sentinelPortSpin);
    m_sentinelTlsCheck = new QCheckBox(tr("Sentinel TLS"), this);
    m_sentinelTlsCheck->setChecked(profile.sentinelTls);
    form->addRow(QString(), m_sentinelTlsCheck);
    m_sshCheck = new QCheckBox(tr("Use SSH tunnel"), this);
    m_sshCheck->setChecked(profile.sshTunnel);
    form->addRow(QString(), m_sshCheck);
    m_sshHostEdit = new QLineEdit(profile.sshHost, this);
    m_sshHostEdit->setPlaceholderText(tr("SSH jump host"));
    form->addRow(tr("SSH host"), m_sshHostEdit);
    m_sshPortSpin = new QSpinBox(this);
    m_sshPortSpin->setRange(1, 65535);
    m_sshPortSpin->setValue(profile.sshPort);
    form->addRow(tr("SSH port"), m_sshPortSpin);
    m_sshUserEdit = new QLineEdit(profile.sshUser, this);
    form->addRow(tr("SSH user"), m_sshUserEdit);
    m_sshKeyEdit = new QLineEdit(profile.sshKeyPath, this);
    m_sshKeyEdit->setPlaceholderText(tr("Optional private key path"));
    form->addRow(tr("SSH key"), m_sshKeyEdit);
    form->addRow(QString(), new QLabel(tr("SSH uses BatchMode: key/agent authentication only."), this));
    form->addRow(QString(), new QLabel(tr("Passwords are stored separately in KWallet."), this));

    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &ProfileDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &ProfileDialog::reject);
}

ConnectionProfile ProfileDialog::profile() const
{
    return m_profile;
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
    m_profile.host = m_hostEdit->text().trimmed();
    m_profile.port = static_cast<quint16>(m_portSpin->value());
    m_profile.tls = m_tlsCheck->isChecked();
    m_profile.username = m_usernameEdit->text();
    m_profile.db = m_dbSpin->value();
    m_profile.readOnly = m_readOnlyCheck->isChecked();
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
    if (m_profile.sentinel
        && (m_profile.sentinelMaster.isEmpty() || m_profile.sentinelHost.isEmpty())) {
        QMessageBox::warning(this, tr("Invalid Sentinel configuration"),
                             tr("Sentinel master and host must not be empty."));
        return;
    }
    QDialog::accept();
}

} // namespace KeyDis
