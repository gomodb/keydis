// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "test_redisinfo.h"

#include "core/insights/redisinfo.h"

#include <QTest>

namespace KeyDis::Test {

void TestRedisInfo::parsesFieldsAndKeyspace()
{
    const QByteArray payload =
        "# Server\r\n"
        "redis_version:7.4.0\r\n"
        "os:Linux 6.12 x86_64\r\n"
        "process_id:1234\r\n"
        "\r\n"
        "# Memory\r\n"
        "used_memory_human:1.20M\r\n"
        "\r\n"
        "# Keyspace\r\n"
        "db0:keys=9,expires=1,avg_ttl=0\r\n"
        "db2:keys=6,expires=2,avg_ttl=479367948800\r\n";

    const RedisInfo info = RedisInfo::parse(payload);
    QCOMPARE(info.value(QStringLiteral("redis_version")), QStringLiteral("7.4.0"));
    QCOMPARE(info.value(QStringLiteral("os")), QStringLiteral("Linux 6.12 x86_64"));
    QCOMPARE(info.value(QStringLiteral("process_id")), QStringLiteral("1234"));
    QCOMPARE(info.value(QStringLiteral("used_memory_human")), QStringLiteral("1.20M"));
    QCOMPARE(info.databases.size(), 2);
    QCOMPARE(info.databases[0].db, 0);
    QCOMPARE(info.databases[0].keys, qint64(9));
    QCOMPARE(info.databases[0].expires, qint64(1));
    QCOMPARE(info.databases[1].db, 2);
    QCOMPARE(info.databases[1].keys, qint64(6));
    QCOMPARE(info.databases[1].avgTtlMs, qint64(479367948800));
    QCOMPARE(info.value(QStringLiteral("missing")), QStringLiteral("-"));
}

} // namespace KeyDis::Test
