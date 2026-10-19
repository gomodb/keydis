// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "ui/dialogs/hyperloglogviewdialog.h"

#include "core/net/redisclient.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace KeyDis {

HyperLogLogViewDialog::HyperLogLogViewDialog(RedisClient *connection, const QByteArray &key,
                                             QWidget *parent)
    : QDialog(parent)
    , m_connection(connection)
    , m_key(key)
{
    setWindowTitle(tr("HyperLogLog: %1").arg(QString::fromUtf8(key)));
    resize(460, 220);

    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("Redis HyperLogLog uses a compact probabilistic encoding."), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    m_status = new QLabel(tr("Querying PFCOUNT..."), this);
    m_status->setObjectName(QStringLiteral("hllStatus"));
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_status);
    layout->addStretch();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto *refreshButton = buttons->addButton(tr("Refresh"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(refreshButton, &QPushButton::clicked, this, &HyperLogLogViewDialog::refresh);

    refresh();
}

void HyperLogLogViewDialog::refresh()
{
    if (!m_connection)
        return;
    m_status->setText(tr("Querying PFCOUNT..."));
    m_connection->sendCommand(
        {QByteArrayLiteral("PFCOUNT"), m_key},
        [this](const RespValue &reply, const QString &error) {
            if (!error.isEmpty() || reply.isError()) {
                m_status->setText(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
                return;
            }
            if (reply.type == RespType::Integer)
                m_status->setText(tr("Cardinality (estimated): %1").arg(reply.integer));
            else
                m_status->setText(QString::fromUtf8(reply.scalar));
        });
}

} // namespace KeyDis
