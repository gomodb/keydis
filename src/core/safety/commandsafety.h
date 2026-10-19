// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QByteArray>
#include <QList>

namespace KeyDis {

enum class CommandDanger {
    Safe,
    Caution,
    Destructive,
};

class CommandSafety
{
public:
    [[nodiscard]] static CommandDanger classify(const QList<QByteArray> &args);
};

} // namespace KeyDis
