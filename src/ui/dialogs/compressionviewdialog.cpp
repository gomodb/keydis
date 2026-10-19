// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "ui/dialogs/compressionviewdialog.h"

#include "core/format/compressioncodec.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QVBoxLayout>

namespace KeyDis {

CompressionViewDialog::CompressionViewDialog(const QString &title, const QByteArray &bytes,
                                             QWidget *parent)
    : QDialog(parent)
    , m_bytes(bytes)
{
    setWindowTitle(title);
    resize(760, 540);

    auto *layout = new QVBoxLayout(this);
    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Format:"), this));
    m_format = new QComboBox(this);
    m_format->setObjectName(QStringLiteral("compressionFormat"));
    m_format->addItem(tr("Auto detect"), int(CompressionFormat::Auto));
    m_format->addItem(QStringLiteral("gzip"), int(CompressionFormat::Gzip));
    m_format->addItem(QStringLiteral("zlib"), int(CompressionFormat::Zlib));
    m_format->addItem(QStringLiteral("zstd"), int(CompressionFormat::Zstd));
    m_format->addItem(QStringLiteral("brotli"), int(CompressionFormat::Brotli));
    top->addWidget(m_format);
    top->addStretch();
    layout->addLayout(top);

    m_view = new QPlainTextEdit(this);
    m_view->setObjectName(QStringLiteral("decodedView"));
    m_view->setReadOnly(true);
    m_view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(m_view, 1);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("decodeStatus"));
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_status);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_format, &QComboBox::currentIndexChanged, this, &CompressionViewDialog::refresh);
    refresh();
}

void CompressionViewDialog::refresh()
{
    const auto format = static_cast<CompressionFormat>(m_format->currentData().toInt());
    QByteArray decoded;
    QString error;
    if (!CompressionCodec::decompress(m_bytes, format, &decoded, &error)) {
        m_status->setStyleSheet(QStringLiteral("color: #da4453;"));
        m_status->setText(error);
        m_view->clear();
        return;
    }
    const CompressionFormat actual = format == CompressionFormat::Auto
        ? CompressionCodec::detect(m_bytes)
        : format;
    m_status->setStyleSheet(QString());
    m_status->setText(tr("%1 · %2 bytes compressed → %3 bytes decoded")
                          .arg(CompressionCodec::name(actual))
                          .arg(m_bytes.size())
                          .arg(decoded.size()));
    m_view->setPlainText(QString::fromUtf8(decoded));
}

} // namespace KeyDis
