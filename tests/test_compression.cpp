// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "test_compression.h"

#include "core/format/compressioncodec.h"

#include <QTest>

namespace KeyDis::Test {

void TestCompression::detectsAndDecompresses()
{
    const QByteArray gzip =
        QByteArray::fromHex("1f8b08000000000002ffcb48cdc9c9070086a6103605000000");
    const QByteArray zstd = QByteArray::fromHex("28b52ffd045829000068656c6c6fa36d9f88");
    const QByteArray brotli = QByteArray::fromHex("0f028068656c6c6f03");
    const QByteArray zlib = qCompress(QByteArrayLiteral("hello")).mid(4);

    QCOMPARE(CompressionCodec::detect(gzip), CompressionFormat::Gzip);
    QCOMPARE(CompressionCodec::detect(zstd), CompressionFormat::Zstd);
    QCOMPARE(CompressionCodec::detect(zlib), CompressionFormat::Zlib);

    QByteArray output;
    QString error;
    QVERIFY2(CompressionCodec::decompress(gzip, CompressionFormat::Auto, &output, &error),
             qPrintable(error));
    QCOMPARE(output, QByteArrayLiteral("hello"));
    QVERIFY2(CompressionCodec::decompress(zlib, CompressionFormat::Zlib, &output, &error),
             qPrintable(error));
    QCOMPARE(output, QByteArrayLiteral("hello"));
    QVERIFY2(CompressionCodec::decompress(zstd, CompressionFormat::Zstd, &output, &error),
             qPrintable(error));
    QCOMPARE(output, QByteArrayLiteral("hello"));
    QVERIFY2(CompressionCodec::decompress(brotli, CompressionFormat::Brotli, &output, &error),
             qPrintable(error));
    QCOMPARE(output, QByteArrayLiteral("hello"));
}

void TestCompression::rejectsInvalidData()
{
    QByteArray output;
    QString error;
    QVERIFY(!CompressionCodec::decompress(QByteArrayLiteral("not compressed"),
                                          CompressionFormat::Gzip, &output, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!CompressionCodec::decompress(QByteArrayLiteral("not compressed"),
                                          CompressionFormat::Auto, &output, &error));
}

} // namespace KeyDis::Test
