// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "ui/dialogs/binaryeditdialog.h"

#include "core/format/byteformat.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QVBoxLayout>

namespace KeyDis {

BinaryEditDialog::BinaryEditDialog(const QString &title, const QByteArray &bytes, QWidget *parent)
    : QDialog(parent)
    , m_bytes(bytes)
{
    setWindowTitle(title);
    resize(720, 500);

    auto *layout = new QVBoxLayout(this);
    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Format:"), this));
    m_mode = new QComboBox(this);
    m_mode->setObjectName(QStringLiteral("binaryMode"));
    m_mode->addItem(QStringLiteral("Hex"), QStringLiteral("hex"));
    m_mode->addItem(QStringLiteral("Base64"), QStringLiteral("base64"));
    top->addWidget(m_mode);
    top->addStretch();
    layout->addLayout(top);

    m_editor = new QPlainTextEdit(this);
    m_editor->setObjectName(QStringLiteral("binaryEdit"));
    m_editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    layout->addWidget(m_editor, 1);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("binaryStatus"));
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_status);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &BinaryEditDialog::acceptIfValid);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_mode, &QComboBox::currentIndexChanged, this, [this] {
        QByteArray parsed;
        QString error;
        if (currentBytes(&parsed, &error))
            m_bytes = parsed;
        formatCurrent();
    });
    connect(m_editor, &QPlainTextEdit::textChanged, this, &BinaryEditDialog::updatePreview);

    formatCurrent();
}

QByteArray BinaryEditDialog::bytes() const
{
    QByteArray parsed;
    QString error;
    return currentBytes(&parsed, &error) ? parsed : m_bytes;
}

bool BinaryEditDialog::currentBytes(QByteArray *bytes, QString *error) const
{
    if (m_mode->currentData().toString() == QLatin1String("base64"))
        return ByteFormat::fromBase64(m_editor->toPlainText(), bytes, error);
    return ByteFormat::fromHex(m_editor->toPlainText(), bytes, error);
}

void BinaryEditDialog::formatCurrent()
{
    m_editor->setPlainText(m_mode->currentData().toString() == QLatin1String("base64")
                               ? ByteFormat::toBase64(m_bytes)
                               : ByteFormat::toHex(m_bytes));
}

void BinaryEditDialog::updatePreview()
{
    QByteArray parsed;
    QString error;
    if (!currentBytes(&parsed, &error)) {
        m_status->setStyleSheet(QStringLiteral("color: #da4453;"));
        m_status->setText(error);
        return;
    }
    QString preview;
    const int limit = qMin(parsed.size(), 48);
    for (int index = 0; index < limit; ++index) {
        const unsigned char byte = static_cast<unsigned char>(parsed.at(index));
        preview += (byte >= 32 && byte < 127) ? QChar(byte) : QLatin1Char('.');
    }
    if (parsed.size() > limit)
        preview += QStringLiteral("…");
    m_status->setStyleSheet(QString());
    m_status->setText(tr("%1 bytes · ASCII: %2").arg(parsed.size()).arg(preview));
}

void BinaryEditDialog::acceptIfValid()
{
    QByteArray parsed;
    QString error;
    if (!currentBytes(&parsed, &error)) {
        QMessageBox::warning(this, tr("Edit bytes"), error);
        return;
    }
    m_bytes = parsed;
    accept();
}

} // namespace KeyDis
