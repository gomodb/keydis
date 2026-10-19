// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "ui/dialogs/protobufviewdialog.h"

#include "core/format/protobufdecoder.h"

#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QLabel>
#include <QPlainTextEdit>
#include <QVBoxLayout>

namespace KeyDis {

ProtobufViewDialog::ProtobufViewDialog(const QString &title, const QByteArray &bytes,
                                       QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(title);
    resize(760, 540);

    auto *layout = new QVBoxLayout(this);
    m_view = new QPlainTextEdit(this);
    m_view->setObjectName(QStringLiteral("protobufView"));
    m_view->setReadOnly(true);
    m_view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(m_view, 1);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("protobufStatus"));
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_status);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QString error;
    const QString decoded = ProtobufDecoder::decode(bytes, &error);
    if (!error.isEmpty()) {
        m_status->setStyleSheet(QStringLiteral("color: #da4453;"));
        m_status->setText(error);
    } else {
        m_status->setText(tr("Protobuf wire format · %1 bytes (schema-less)").arg(bytes.size()));
        m_view->setPlainText(decoded);
    }
}

} // namespace KeyDis
