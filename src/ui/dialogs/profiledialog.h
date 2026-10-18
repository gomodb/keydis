// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include "core/conn/connectionprofile.h"

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QSpinBox;

namespace KeyDis {

class ProfileDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ProfileDialog(const ConnectionProfile &profile, QWidget *parent = nullptr);

    [[nodiscard]] ConnectionProfile profile() const;

private:
    void accept() override;

    ConnectionProfile m_profile;
    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_groupEdit = nullptr;
    QLineEdit *m_hostEdit = nullptr;
    QSpinBox *m_portSpin = nullptr;
    QCheckBox *m_tlsCheck = nullptr;
    QLineEdit *m_usernameEdit = nullptr;
    QLineEdit *m_passwordEdit = nullptr;
    QSpinBox *m_dbSpin = nullptr;
    QCheckBox *m_readOnlyCheck = nullptr;
    QCheckBox *m_clusterCheck = nullptr;
    QCheckBox *m_sentinelCheck = nullptr;
    QLineEdit *m_sentinelMasterEdit = nullptr;
    QLineEdit *m_sentinelHostEdit = nullptr;
    QSpinBox *m_sentinelPortSpin = nullptr;
    QCheckBox *m_sentinelTlsCheck = nullptr;
    QCheckBox *m_sshCheck = nullptr;
    QLineEdit *m_sshHostEdit = nullptr;
    QSpinBox *m_sshPortSpin = nullptr;
    QLineEdit *m_sshUserEdit = nullptr;
    QLineEdit *m_sshKeyEdit = nullptr;
};

} // namespace KeyDis
