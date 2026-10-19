// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "core/safety/commandsafety.h"

namespace KeyDis {

CommandDanger CommandSafety::classify(const QList<QByteArray> &args)
{
    if (args.isEmpty())
        return CommandDanger::Safe;
    const QByteArray command = args.first().toUpper();
    const QByteArray subcommand = args.size() > 1 ? args.at(1).toUpper() : QByteArray();

    if (command == QByteArrayLiteral("FLUSHALL") || command == QByteArrayLiteral("FLUSHDB")
        || command == QByteArrayLiteral("SHUTDOWN") || command == QByteArrayLiteral("DEBUG")
        || command == QByteArrayLiteral("SWAPDB") || command == QByteArrayLiteral("REPLICAOF")
        || command == QByteArrayLiteral("SLAVEOF") || command == QByteArrayLiteral("MIGRATE")) {
        return CommandDanger::Destructive;
    }
    if (command == QByteArrayLiteral("SCRIPT") && subcommand == QByteArrayLiteral("FLUSH"))
        return CommandDanger::Destructive;
    if (command == QByteArrayLiteral("FUNCTION")
        && (subcommand == QByteArrayLiteral("FLUSH") || subcommand == QByteArrayLiteral("DELETE")))
        return CommandDanger::Destructive;
    if (command == QByteArrayLiteral("ACL")
        && (subcommand == QByteArrayLiteral("SETUSER") || subcommand == QByteArrayLiteral("DELUSER")))
        return CommandDanger::Destructive;
    if (command == QByteArrayLiteral("CLIENT") && subcommand == QByteArrayLiteral("KILL"))
        return CommandDanger::Destructive;
    if (command == QByteArrayLiteral("CONFIG")
        && (subcommand == QByteArrayLiteral("SET") || subcommand == QByteArrayLiteral("RESETSTAT")
            || subcommand == QByteArrayLiteral("REWRITE")))
        return CommandDanger::Destructive;
    if (command == QByteArrayLiteral("MODULE") && subcommand == QByteArrayLiteral("UNLOAD"))
        return CommandDanger::Destructive;
    if (command == QByteArrayLiteral("RESTORE") || command == QByteArrayLiteral("RENAME")
        || command == QByteArrayLiteral("RENAMENX"))
        return CommandDanger::Caution;

    if (command == QByteArrayLiteral("KEYS") || command == QByteArrayLiteral("MONITOR")
        || command == QByteArrayLiteral("EVAL") || command == QByteArrayLiteral("EVALSHA")
        || command == QByteArrayLiteral("FCALL") || command == QByteArrayLiteral("FCALL_RO")
        || command == QByteArrayLiteral("FUNCTION") || command == QByteArrayLiteral("SCRIPT")
        || command == QByteArrayLiteral("BGSAVE") || command == QByteArrayLiteral("BGREWRITEAOF")
        || command == QByteArrayLiteral("CLIENT") || command == QByteArrayLiteral("CONFIG")) {
        return CommandDanger::Caution;
    }
    return CommandDanger::Safe;
}

} // namespace KeyDis
