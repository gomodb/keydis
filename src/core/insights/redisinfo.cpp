// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "core/insights/redisinfo.h"

#include <QList>

namespace KeyDis {

QString RedisInfo::value(const QString &key, const QString &fallback) const
{
    const auto it = fields.constFind(key);
    return it == fields.cend() ? fallback : it.value();
}

RedisInfo RedisInfo::parse(const QByteArray &payload)
{
    RedisInfo info;
    const QList<QByteArray> lines = payload.split('\n');
    for (QByteArray line : lines) {
        if (line.endsWith('\r'))
            line.chop(1);
        if (line.isEmpty() || line.startsWith('#'))
            continue;

        const int colon = line.indexOf(':');
        if (colon <= 0)
            continue;
        const QString key = QString::fromLatin1(line.left(colon));
        const QString value = QString::fromUtf8(line.mid(colon + 1));

        if (key.startsWith(QStringLiteral("db")) && key.size() > 2) {
            bool ok = false;
            const int db = key.mid(2).toInt(&ok);
            if (!ok)
                continue;
            KeyspaceInfo keyspace;
            keyspace.db = db;
            const QList<QByteArray> parts = line.mid(colon + 1).split(',');
            for (const QByteArray &part : parts) {
                const int equals = part.indexOf('=');
                if (equals <= 0)
                    continue;
                const QByteArray name = part.left(equals);
                const qint64 number = part.mid(equals + 1).toLongLong();
                if (name == "keys")
                    keyspace.keys = number;
                else if (name == "expires")
                    keyspace.expires = number;
                else if (name == "avg_ttl")
                    keyspace.avgTtlMs = number;
            }
            info.databases.append(keyspace);
        } else {
            info.fields.insert(key, value);
        }
    }
    return info;
}

} // namespace KeyDis
