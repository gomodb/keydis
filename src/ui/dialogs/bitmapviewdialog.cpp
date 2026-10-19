// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "ui/dialogs/bitmapviewdialog.h"

#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QLabel>
#include <QPlainTextEdit>
#include <QVBoxLayout>

namespace KeyDis {

BitmapViewDialog::BitmapViewDialog(const QString &title, const QByteArray &bytes, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(title);
    resize(760, 540);

    qint64 bitCount = 0;
    for (const char byte : bytes) {
        unsigned int value = static_cast<unsigned char>(byte);
        while (value) {
            bitCount += value & 1U;
            value >>= 1U;
        }
    }

    auto *layout = new QVBoxLayout(this);
    m_view = new QPlainTextEdit(this);
    m_view->setObjectName(QStringLiteral("bitmapView"));
    m_view->setReadOnly(true);
    m_view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_view->setLineWrapMode(QPlainTextEdit::NoWrap);

    const int previewBytes = qMin(bytes.size(), 4096);
    QString text;
    for (int offset = 0; offset < previewBytes; offset += 8) {
        QString hex;
        QString bits;
        QString ascii;
        const int count = qMin(8, previewBytes - offset);
        for (int index = 0; index < count; ++index) {
            const unsigned int value = static_cast<unsigned char>(bytes.at(offset + index));
            hex += QStringLiteral("%1 ").arg(value, 2, 16, QLatin1Char('0')).toUpper();
            bits += QStringLiteral("%1 ").arg(value, 8, 2, QLatin1Char('0'));
            ascii += (value >= 32 && value < 127) ? QChar(value) : QLatin1Char('.');
        }
        text += QStringLiteral("%1  %2 %3 %4\n")
                    .arg(offset, 8, 16, QLatin1Char('0'))
                    .arg(hex, -24)
                    .arg(bits, -72)
                    .arg(ascii);
    }
    if (bytes.size() > previewBytes)
        text += QStringLiteral("…\n");
    m_view->setPlainText(text);

    m_status = new QLabel(tr("%1 bytes · %2 bits set · %3 bits total")
                              .arg(bytes.size())
                              .arg(bitCount)
                              .arg(qint64(bytes.size()) * 8),
                          this);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_view, 1);
    layout->addWidget(m_status);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

} // namespace KeyDis
