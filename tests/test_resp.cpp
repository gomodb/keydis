// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "test_resp.h"

#include "core/resp/hiredisreader.h"

#include <QTest>

namespace KeyDis::Test {

namespace {

bool readOne(HiredisReader &reader, RespValue *value, QString *error)
{
    return reader.read(value, error) == HiredisReader::ReadResult::Reply;
}

bool feed(HiredisReader &reader, const QByteArray &data, QString *error)
{
    return reader.feed(data.constData(), data.size(), error);
}

} // namespace

void TestResp::parsesSimpleTypes()
{
    HiredisReader reader;
    QString error;
    RespValue value;

    QVERIFY2(feed(reader, "+OK\r\n", &error), qPrintable(error));
    QVERIFY(readOne(reader, &value, &error));
    QVERIFY(value.type == RespType::SimpleString);
    QCOMPARE(value.scalar, QByteArray("OK"));

    QVERIFY2(feed(reader, "-ERR bad\r\n", &error), qPrintable(error));
    QVERIFY(readOne(reader, &value, &error));
    QVERIFY(value.type == RespType::Error);
    QCOMPARE(value.scalar, QByteArray("ERR bad"));

    QVERIFY2(feed(reader, ":42\r\n", &error), qPrintable(error));
    QVERIFY(readOne(reader, &value, &error));
    QVERIFY(value.type == RespType::Integer);
    QCOMPARE(value.integer, qint64(42));

    QVERIFY2(feed(reader, "$5\r\nhello\r\n", &error), qPrintable(error));
    QVERIFY(readOne(reader, &value, &error));
    QVERIFY(value.type == RespType::BulkString);
    QCOMPARE(value.scalar, QByteArray("hello"));

    QVERIFY2(feed(reader, "$-1\r\n", &error), qPrintable(error));
    QVERIFY(readOne(reader, &value, &error));
    QVERIFY(value.type == RespType::Null);
}

void TestResp::parsesAggregates()
{
    HiredisReader reader;
    QString error;
    RespValue value;

    QVERIFY2(feed(reader, "*2\r\n$3\r\nfoo\r\n:1\r\n", &error), qPrintable(error));
    QVERIFY(readOne(reader, &value, &error));
    QVERIFY(value.type == RespType::Array);
    QCOMPARE(value.elements.size(), size_t(2));
    QCOMPARE(value.elements[0].scalar, QByteArray("foo"));
    QCOMPARE(value.elements[1].integer, qint64(1));

    QVERIFY2(feed(reader, "%2\r\n+one\r\n:1\r\n+two\r\n:2\r\n", &error), qPrintable(error));
    QVERIFY(readOne(reader, &value, &error));
    QVERIFY(value.type == RespType::Map);
    QCOMPARE(value.pairs.size(), size_t(2));
    QCOMPARE(value.pairs[1].first.scalar, QByteArray("two"));
    QCOMPARE(value.pairs[1].second.integer, qint64(2));

    QVERIFY2(feed(reader, "~2\r\n+alpha\r\n+beta\r\n", &error), qPrintable(error));
    QVERIFY(readOne(reader, &value, &error));
    QVERIFY(value.type == RespType::Set);
    QCOMPARE(value.elements.size(), size_t(2));
}

void TestResp::parsesResp3Types()
{
    HiredisReader reader;
    QString error;
    RespValue value;

    QVERIFY2(feed(reader, ",3.5\r\n", &error), qPrintable(error));
    QVERIFY(readOne(reader, &value, &error));
    QVERIFY(value.type == RespType::Double);
    QCOMPARE(value.real, 3.5);

    QVERIFY2(feed(reader, "#t\r\n", &error), qPrintable(error));
    QVERIFY(readOne(reader, &value, &error));
    QVERIFY(value.type == RespType::Boolean);
    QCOMPARE(value.integer, qint64(1));

    QVERIFY2(feed(reader, "(12345678901234567890\r\n", &error), qPrintable(error));
    QVERIFY(readOne(reader, &value, &error));
    QVERIFY(value.type == RespType::BigNumber);
    QCOMPARE(value.scalar, QByteArray("12345678901234567890"));

    QVERIFY2(feed(reader, "=9\r\ntxt:hello\r\n", &error), qPrintable(error));
    QVERIFY(readOne(reader, &value, &error));
    QVERIFY(value.type == RespType::Verbatim);
    QCOMPARE(value.verbatimType, QStringLiteral("txt"));
    QCOMPARE(value.scalar, QByteArray("hello"));

    QVERIFY2(feed(reader, ">2\r\n+message\r\n+hello\r\n", &error), qPrintable(error));
    QVERIFY(readOne(reader, &value, &error));
    QVERIFY(value.type == RespType::Push);
    QCOMPARE(value.elements.size(), size_t(2));
}

void TestResp::handlesFragmentedInput()
{
    const QByteArray payload = "*2\r\n$3\r\nfoo\r\n:7\r\n";
    HiredisReader reader;
    QString error;
    RespValue value;
    bool gotReply = false;

    for (char byte : payload) {
        QVERIFY2(reader.feed(&byte, 1, &error), qPrintable(error));
        const auto result = reader.read(&value, &error);
        if (result == HiredisReader::ReadResult::Error)
            QFAIL(qPrintable(error));
        if (result == HiredisReader::ReadResult::Reply) {
            gotReply = true;
            break;
        }
    }

    QVERIFY(gotReply);
    QVERIFY(value.type == RespType::Array);
    QCOMPARE(value.elements[1].integer, qint64(7));
}

void TestResp::rejectsOversizedReply()
{
    HiredisReader reader(8);
    QString error;
    QVERIFY(!feed(reader, "+123456789\r\n", &error));
    QVERIFY(!error.isEmpty());
}

void TestResp::rejectsTooManyElements()
{
    HiredisReader reader(1024, 2);
    QString error;
    RespValue value;
    QVERIFY2(feed(reader, "*3\r\n+one\r\n+two\r\n+three\r\n", &error), qPrintable(error));
    QVERIFY(reader.read(&value, &error) == HiredisReader::ReadResult::Error);
    QVERIFY(error.contains(QStringLiteral("too many elements"))
            || error.contains(QStringLiteral("out of range")));
}

void TestResp::rejectsTooDeepNesting()
{
    HiredisReader reader(1024, 100, 2);
    QString error;
    RespValue value;
    QVERIFY2(feed(reader, "*1\r\n*1\r\n*1\r\n+OK\r\n", &error), qPrintable(error));
    QVERIFY(reader.read(&value, &error) == HiredisReader::ReadResult::Error);
    QVERIFY(error.contains(QStringLiteral("nesting")));
}

} // namespace KeyDis::Test
