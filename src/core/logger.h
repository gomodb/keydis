// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sunhowcn
// Derived from NutSync (https://github.com/gomodb/nutsync),
// commit f7fd60730110f942911e56eefa8c6771abb3eb2a,
// originally GPL-3.0-or-later; relicensed to MIT by the copyright holder.

#pragma once

#include <QString>

namespace KeyDis {

class Logger
{
public:
    static void install();
    static void setLevel(const QString &level);
    static void setRetention(int maxSizeMb, int files);
    static QString level();
    static void log(const QString &category, const QString &message);
    static QString logFilePath();
    static QString redact(const QString &message);
};

} // namespace KeyDis
