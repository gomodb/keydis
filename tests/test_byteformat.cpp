// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "test_byteformat.h"

#include "core/format/byteformat.h"

#include <QTest>

namespace KeyDis::Test {

void TestByteFormat::roundTripsHexAndBase64()
{
    const QByteArray bytes = QByteArray::fromHex("0001ff10") + QByteArray("hello", 5);
    QByteArray parsed;
    QString error;
    QVERIFY(ByteFormat::fromHex(ByteFormat::toHex(bytes), &parsed, &error));
    QCOMPARE(parsed, bytes);
    QVERIFY(ByteFormat::fromHex(QStringLiteral("0x00 01 ff 10"), &parsed, &error));
    QCOMPARE(parsed, QByteArray::fromHex("0001ff10"));

    QVERIFY(ByteFormat::fromBase64(ByteFormat::toBase64(bytes), &parsed, &error));
    QCOMPARE(parsed, bytes);
}

void TestByteFormat::rejectsInvalidInput()
{
    QByteArray parsed;
    QString error;
    QVERIFY(!ByteFormat::fromHex(QStringLiteral("0"), &parsed, &error));
    QVERIFY(!ByteFormat::fromHex(QStringLiteral("zz"), &parsed, &error));
    QVERIFY(!ByteFormat::fromBase64(QStringLiteral("!!!!"), &parsed, &error));
}

} // namespace KeyDis::Test
