// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sunhowcn
// Derived from NutSync (https://github.com/gomodb/nutsync),
// commit f7fd60730110f942911e56eefa8c6771abb3eb2a,
// originally GPL-3.0-or-later; relicensed to MIT by the copyright holder.

#include "core/app_paths.h"

#include <QDir>
#include <QStandardPaths>

namespace KeyDis::AppPaths {

namespace {

QString xdgDir(QStandardPaths::StandardLocation location, const QString &fallbackSuffix)
{
    QString base = QStandardPaths::writableLocation(location);
    if (base.isEmpty())
        base = QDir::homePath() + fallbackSuffix;
    return base + QStringLiteral("/keydis");
}

} // namespace

QString configDir()
{
    return xdgDir(QStandardPaths::GenericConfigLocation, QStringLiteral("/.config"));
}

QString configFile()
{
    return configDir() + QStringLiteral("/connections.json");
}

QString dataDir()
{
    return xdgDir(QStandardPaths::GenericDataLocation, QStringLiteral("/.local/share"));
}

QString stateDir()
{
    return xdgDir(QStandardPaths::GenericStateLocation, QStringLiteral("/.local/state"));
}

QString sessionFile()
{
    return stateDir() + QStringLiteral("/session.json");
}

QString logFile()
{
    return stateDir() + QStringLiteral("/keydis.log");
}

QString cacheDir()
{
    return xdgDir(QStandardPaths::GenericCacheLocation, QStringLiteral("/.cache"));
}

void ensureDirectories()
{
    QDir().mkpath(configDir());
    QDir().mkpath(dataDir());
    QDir().mkpath(stateDir());
    QDir().mkpath(cacheDir());
}

} // namespace KeyDis::AppPaths
