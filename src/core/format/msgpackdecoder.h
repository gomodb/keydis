// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QByteArray>
#include <QString>

namespace KeyDis {

class MsgPackDecoder
{
public:
    [[nodiscard]] static QString decode(const QByteArray &data, QString *error = nullptr);
};

} // namespace KeyDis
