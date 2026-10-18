// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>

namespace KeyDis {

struct KeyspaceInfo {
    int db = 0;
    qint64 keys = 0;
    qint64 expires = 0;
    qint64 avgTtlMs = 0;
};

struct RedisInfo {
    QHash<QString, QString> fields;
    QVector<KeyspaceInfo> databases;

    [[nodiscard]] QString value(const QString &key,
                                const QString &fallback = QStringLiteral("-")) const;
    static RedisInfo parse(const QByteArray &payload);
};

} // namespace KeyDis
