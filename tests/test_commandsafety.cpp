// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "test_commandsafety.h"

#include "core/safety/commandsafety.h"

#include <QTest>

namespace KeyDis::Test {

void TestCommandSafety::classifiesCommands()
{
    QCOMPARE(CommandSafety::classify({QByteArrayLiteral("GET"), QByteArrayLiteral("key")}),
             CommandDanger::Safe);
    QCOMPARE(CommandSafety::classify({QByteArrayLiteral("KEYS"), QByteArrayLiteral("*")}),
             CommandDanger::Caution);
    QCOMPARE(CommandSafety::classify({QByteArrayLiteral("EVAL"), QByteArrayLiteral("return 1"),
                                      QByteArrayLiteral("0")}),
             CommandDanger::Caution);
    QCOMPARE(CommandSafety::classify({QByteArrayLiteral("FLUSHALL")}),
             CommandDanger::Destructive);
    QCOMPARE(CommandSafety::classify({QByteArrayLiteral("CONFIG"), QByteArrayLiteral("SET"),
                                      QByteArrayLiteral("maxmemory"), QByteArrayLiteral("0")}),
             CommandDanger::Destructive);
    QCOMPARE(CommandSafety::classify({QByteArrayLiteral("SCRIPT"), QByteArrayLiteral("FLUSH")}),
             CommandDanger::Destructive);
    QCOMPARE(CommandSafety::classify({QByteArrayLiteral("ACL"), QByteArrayLiteral("SETUSER"),
                                      QByteArrayLiteral("alice"), QByteArrayLiteral("on")}),
             CommandDanger::Destructive);
    QCOMPARE(CommandSafety::classify({QByteArrayLiteral("RESTORE"), QByteArrayLiteral("key"),
                                      QByteArrayLiteral("0"), QByteArrayLiteral("value")}),
             CommandDanger::Caution);
}

} // namespace KeyDis::Test
