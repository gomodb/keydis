// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "test_protobuf.h"

#include "core/format/protobufdecoder.h"

#include <QTest>

namespace KeyDis::Test {

void TestProtobuf::decodesWireFields()
{
    const QByteArray packed = QByteArray::fromHex("082a120268691a020807");
    QString error;
    const QString decoded = ProtobufDecoder::decode(packed, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(decoded.contains(QStringLiteral("1 (varint): 42")));
    QVERIFY(decoded.contains(QStringLiteral("2 (length-delimited): \"hi\"")));
    QVERIFY(decoded.contains(QStringLiteral("3 (length-delimited): {")));
    QVERIFY(decoded.contains(QStringLiteral("1 (varint): 7")));
}

void TestProtobuf::rejectsInvalidData()
{
    QString error;
    QVERIFY(ProtobufDecoder::decode(QByteArray::fromHex("08"), &error).isEmpty());
    QVERIFY(!error.isEmpty());
    error.clear();
    QVERIFY(ProtobufDecoder::decode(QByteArray::fromHex("0f"), &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

} // namespace KeyDis::Test
