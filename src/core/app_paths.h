// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sunhowcn
// Derived from NutSync (https://github.com/gomodb/nutsync),
// commit f7fd60730110f942911e56eefa8c6771abb3eb2a,
// originally GPL-3.0-or-later; relicensed to MIT by the copyright holder.

#pragma once

#include <QString>

namespace KeyDis::AppPaths {

QString configDir();
QString configFile();
QString dataDir();
QString stateDir();
QString sessionFile();
QString logFile();
QString cacheDir();

void ensureDirectories();

} // namespace KeyDis::AppPaths
