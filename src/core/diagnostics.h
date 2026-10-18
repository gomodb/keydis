// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sunhowcn
// Derived from NutSync (https://github.com/gomodb/nutsync),
// commit f7fd60730110f942911e56eefa8c6771abb3eb2a,
// originally GPL-3.0-or-later; relicensed to MIT by the copyright holder.

#pragma once

#include <QString>

namespace KeyDis::Diagnostics {

QString buildReport();
QString readLogTail(const QString &logPath, int maxLines = 200);

} // namespace KeyDis::Diagnostics
