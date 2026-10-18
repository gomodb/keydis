// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "test_commandencoder.h"

#include "core/resp/commandencoder.h"

#include <QTest>

namespace KeyDis::Test {

void TestCommandEncoder::encodesArguments()
{
    const QByteArray wire =
        CommandEncoder::encode({QByteArrayLiteral("SET"), QByteArrayLiteral("foo"),
                                QByteArrayLiteral("bar")});
    QCOMPARE(wire, QByteArray("*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n"));
}

void TestCommandEncoder::parsesQuotedArguments()
{
    QList<QByteArray> args;
    QString error;
    QVERIFY2(CommandEncoder::parseLine(
                 QStringLiteral("SET \"hello world\" 'a b' plain\\ value"), &args, &error),
             qPrintable(error));
    QCOMPARE(args.size(), 4);
    QCOMPARE(args[0], QByteArray("SET"));
    QCOMPARE(args[1], QByteArray("hello world"));
    QCOMPARE(args[2], QByteArray("a b"));
    QCOMPARE(args[3], QByteArray("plain value"));

    QVERIFY2(CommandEncoder::parseLine(QStringLiteral("SET key \"line\\nvalue\""), &args, &error),
             qPrintable(error));
    QCOMPARE(args[2], QByteArray("line\nvalue"));

    QVERIFY2(CommandEncoder::parseLine(QStringLiteral("SET empty \"\""), &args, &error),
             qPrintable(error));
    QCOMPARE(args[2], QByteArray(""));
}

void TestCommandEncoder::rejectsUnterminatedQuote()
{
    QList<QByteArray> args;
    QString error;
    QVERIFY(!CommandEncoder::parseLine(QStringLiteral("SET \"unterminated"), &args, &error));
    QVERIFY(!error.isEmpty());
}

} // namespace KeyDis::Test
