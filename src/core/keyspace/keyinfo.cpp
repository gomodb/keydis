// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "core/keyspace/keyinfo.h"

#include <QCoreApplication>

namespace KeyDis {

QString formatTtl(qint64 ttlMs)
{
    if (ttlMs == -1)
        return QCoreApplication::translate("KeyInfo", "persistent");
    if (ttlMs < 0)
        return QCoreApplication::translate("KeyInfo", "missing");

    const qint64 seconds = ttlMs / 1000;
    if (seconds < 60)
        return QCoreApplication::translate("KeyInfo", "%1 s").arg(seconds);
    if (seconds < 3600)
        return QCoreApplication::translate("KeyInfo", "%1 min").arg(seconds / 60);
    if (seconds < 86400)
        return QCoreApplication::translate("KeyInfo", "%1 h").arg(seconds / 3600);
    return QCoreApplication::translate("KeyInfo", "%1 d").arg(seconds / 86400);
}

QString KeyInfo::ttlText() const
{
    return formatTtl(ttlMs);
}

} // namespace KeyDis
