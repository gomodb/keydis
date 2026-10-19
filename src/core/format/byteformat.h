// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QByteArray>
#include <QString>

namespace KeyDis {

class ByteFormat
{
public:
    [[nodiscard]] static QString toHex(const QByteArray &bytes);
    [[nodiscard]] static bool fromHex(const QString &text, QByteArray *bytes,
                                      QString *error = nullptr);

    [[nodiscard]] static QString toBase64(const QByteArray &bytes);
    [[nodiscard]] static bool fromBase64(const QString &text, QByteArray *bytes,
                                         QString *error = nullptr);
};

} // namespace KeyDis
