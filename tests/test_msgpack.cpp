// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "test_msgpack.h"

#include "core/format/msgpackdecoder.h"

#include <QTest>

namespace KeyDis::Test {

void TestMsgPack::decodesObjects()
{
    const QByteArray packed = QByteArray::fromHex(
        "83a16101a16293c3a178c0a16302");
    QString error;
    const QString decoded = MsgPackDecoder::decode(packed, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(decoded.contains(QStringLiteral("\"a\": 1")));
    QVERIFY(decoded.contains(QStringLiteral("\"b\": [")));
    QVERIFY(decoded.contains(QStringLiteral("true")));
    QVERIFY(decoded.contains(QStringLiteral("\"x\"")));
    QVERIFY(decoded.contains(QStringLiteral("null")));
    QVERIFY(decoded.contains(QStringLiteral("\"c\": 2")));

    const QByteArray binary = QByteArray::fromHex("c4030001ff");
    const QString binaryText = MsgPackDecoder::decode(binary, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(binaryText.contains(QStringLiteral("binary 3 bytes")));
}

void TestMsgPack::rejectsInvalidData()
{
    QString error;
    QVERIFY(MsgPackDecoder::decode(QByteArray::fromHex("c1"), &error).isEmpty());
    QVERIFY(!error.isEmpty());
    error.clear();
    QVERIFY(MsgPackDecoder::decode(QByteArray::fromHex("01ff"), &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

} // namespace KeyDis::Test
