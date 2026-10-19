// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "test_valuepanel.h"

#include "core/net/redisclient.h"
#include "core/keyspace/keyinfo.h"
#include "ui/values/valuepanel.h"
#include "ui/dialogs/bitmapviewdialog.h"
#include "ui/dialogs/geosearchdialog.h"
#include "ui/dialogs/hyperloglogviewdialog.h"
#include "ui/dialogs/protobufviewdialog.h"

#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTest>

namespace KeyDis::Test {

namespace {

constexpr qint64 kLargeSize = 2 * 1024 * 1024 + 123;

class FakeRedisClient : public RedisClient
{
public:
    QList<QList<QByteArray>> commands;

    void connectToHost(const QString &, quint16, bool, const QString &, const QString &,
                       int) override
    {
    }
    void disconnectFromHost() override {}
    [[nodiscard]] bool isReady() const override { return true; }

    void sendCommand(const QList<QByteArray> &args, ReplyCallback callback) override
    {
        commands.append(args);
        RespValue reply;
        if (args.value(1) == QByteArrayLiteral("large")
            && args.value(0) == QByteArrayLiteral("STRLEN")) {
            reply.type = RespType::Integer;
            reply.integer = kLargeSize;
        } else if (args.value(1) == QByteArrayLiteral("large")
                   && args.value(0) == QByteArrayLiteral("GETRANGE")) {
            const qint64 start = args.value(2).toLongLong();
            const qint64 end = args.value(3).toLongLong();
            reply.type = RespType::BulkString;
            reply.scalar = QByteArray(int(qMin(end - start + 1, kLargeSize - start)), 'x');
        } else if (args.value(1) == QByteArrayLiteral("large")
                   && args.value(0) == QByteArrayLiteral("GET")) {
            reply.type = RespType::BulkString;
            reply.scalar = QByteArray(int(kLargeSize), 'x');
        } else if (!args.isEmpty() && args.first() == QByteArrayLiteral("JSON.GET")) {
            reply.type = RespType::BulkString;
            reply.scalar = R"({"name":"Alice","tags":["a","b"],"active":true})";
        } else if (args.value(0) == QByteArrayLiteral("PFCOUNT")) {
            reply.type = RespType::Integer;
            reply.integer = 3;
        } else if (args.value(0) == QByteArrayLiteral("GEOSEARCH")) {
            RespValue entry;
            entry.type = RespType::Array;
            RespValue member;
            member.type = RespType::BulkString;
            member.scalar = QByteArrayLiteral("Palermo");
            RespValue distance;
            distance.type = RespType::Double;
            distance.real = 1.25;
            RespValue coords;
            coords.type = RespType::Array;
            RespValue lon;
            lon.type = RespType::Double;
            lon.real = 13.36;
            RespValue lat;
            lat.type = RespType::Double;
            lat.real = 38.11;
            coords.elements = {lon, lat};
            entry.elements = {member, distance, coords};
            reply.type = RespType::Array;
            reply.elements = {entry};
        } else if (args.value(0) == QByteArrayLiteral("XINFO")
                   && args.value(1) == QByteArrayLiteral("GROUPS")) {
            RespValue map;
            map.type = RespType::Map;
            RespValue nameKey;
            nameKey.type = RespType::BulkString;
            nameKey.scalar = QByteArrayLiteral("name");
            RespValue name;
            name.type = RespType::BulkString;
            name.scalar = QByteArrayLiteral("group1");
            RespValue consumersKey;
            consumersKey.type = RespType::BulkString;
            consumersKey.scalar = QByteArrayLiteral("consumers");
            RespValue consumers;
            consumers.type = RespType::BulkString;
            consumers.scalar = QByteArrayLiteral("1");
            RespValue pendingKey;
            pendingKey.type = RespType::BulkString;
            pendingKey.scalar = QByteArrayLiteral("pending");
            RespValue pending;
            pending.type = RespType::BulkString;
            pending.scalar = QByteArrayLiteral("0");
            RespValue lastIdKey;
            lastIdKey.type = RespType::BulkString;
            lastIdKey.scalar = QByteArrayLiteral("last-delivered-id");
            RespValue lastId;
            lastId.type = RespType::BulkString;
            lastId.scalar = QByteArrayLiteral("0-0");
            map.mapKeys = {QByteArrayLiteral("name"), QByteArrayLiteral("consumers"),
                           QByteArrayLiteral("pending"),
                           QByteArrayLiteral("last-delivered-id")};
            map.mapValues = {name, consumers, pending, lastId};
            reply.type = RespType::Array;
            reply.elements = {map};
        } else if (args.value(0) == QByteArrayLiteral("XINFO")
                   && args.value(1) == QByteArrayLiteral("CONSUMERS")) {
            RespValue map;
            map.type = RespType::Map;
            RespValue nameKey;
            nameKey.type = RespType::BulkString;
            nameKey.scalar = QByteArrayLiteral("name");
            RespValue name;
            name.type = RespType::BulkString;
            name.scalar = QByteArrayLiteral("worker");
            RespValue pendingKey;
            pendingKey.type = RespType::BulkString;
            pendingKey.scalar = QByteArrayLiteral("pending");
            RespValue pending;
            pending.type = RespType::BulkString;
            pending.scalar = QByteArrayLiteral("1");
            RespValue idleKey;
            idleKey.type = RespType::BulkString;
            idleKey.scalar = QByteArrayLiteral("idle");
            RespValue idle;
            idle.type = RespType::Integer;
            idle.integer = 120;
            map.mapKeys = {QByteArrayLiteral("name"), QByteArrayLiteral("pending"),
                           QByteArrayLiteral("idle")};
            map.mapValues = {name, pending, idle};
            reply.type = RespType::Array;
            reply.elements = {map};
        } else if (args.value(0) == QByteArrayLiteral("XRANGE")) {
            reply.type = RespType::Array;
        } else if (args.value(0) == QByteArrayLiteral("XPENDING")) {
            reply.type = RespType::Array;
        } else {
            reply.type = RespType::SimpleString;
            reply.scalar = QByteArrayLiteral("OK");
        }
        if (callback)
            callback(reply, {});
    }

    void sendTransaction(const QList<QList<QByteArray>> &commands, ReplyCallback callback) override
    {
        RespValue replies;
        replies.type = RespType::Array;
        for (const QList<QByteArray> &command : commands) {
            this->commands.append(command);
            RespValue reply;
            reply.type = RespType::SimpleString;
            reply.scalar = QByteArrayLiteral("OK");
            replies.elements.push_back(reply);
        }
        if (callback)
            callback(replies, {});
    }
};

QTreeWidgetItem *findItem(QTreeWidgetItem *parent, const QString &path)
{
    if (!parent)
        return nullptr;
    if (parent->data(0, Qt::UserRole).toString() == path)
        return parent;
    for (int index = 0; index < parent->childCount(); ++index) {
        if (QTreeWidgetItem *found = findItem(parent->child(index), path))
            return found;
    }
    return nullptr;
}

} // namespace

void TestValuePanel::buildsAndEditsJsonTree()
{
    FakeRedisClient client;
    ValuePanel panel(&client);
    panel.loadKey(KeyInfo{QByteArrayLiteral("json:test"), QStringLiteral("ReJSON-RL"), -1});

    auto *tree = panel.findChild<QTreeWidget *>(QStringLiteral("jsonTree"));
    QVERIFY(tree);
    QCOMPARE(tree->topLevelItemCount(), 1);
    QTreeWidgetItem *root = tree->topLevelItem(0);
    QVERIFY(root);
    QCOMPARE(root->text(0), QStringLiteral("$"));
    QCOMPARE(root->childCount(), 3);
    QTreeWidgetItem *name = findItem(root, QStringLiteral("$.name"));
    QVERIFY(name);
    QCOMPARE(name->text(1), QStringLiteral("\"Alice\""));
    QTreeWidgetItem *tags = findItem(root, QStringLiteral("$.tags"));
    QVERIFY(tags);
    QCOMPARE(tags->childCount(), 2);

    auto *jsonEdit = panel.findChild<QPlainTextEdit *>(QStringLiteral("jsonEdit"));
    auto *discardJson = panel.findChild<QPushButton *>(QStringLiteral("discardJsonButton"));
    auto *dirtyLabel = panel.findChild<QLabel *>(QStringLiteral("dirtyLabel"));
    QVERIFY(jsonEdit);
    QVERIFY(discardJson);
    QVERIFY(dirtyLabel);
    const QString originalJson = jsonEdit->toPlainText();
    jsonEdit->setPlainText(QStringLiteral("{\"changed\":true}"));
    QVERIFY(!dirtyLabel->isHidden());
    QVERIFY(discardJson->isEnabled());
    QTest::mouseClick(discardJson, Qt::LeftButton);
    QCOMPARE(jsonEdit->toPlainText(), originalJson);

    name = findItem(tree->topLevelItem(0), QStringLiteral("$.name"));
    QVERIFY(name);
    name->setText(1, QStringLiteral("\"Bob\""));
    bool foundSet = false;
    for (const auto &command : client.commands) {
        if (command.value(0) == QByteArrayLiteral("JSON.SET")
            && command.value(2) == QByteArrayLiteral("$.name")
            && command.value(3) == QByteArrayLiteral("\"Bob\"")) {
            foundSet = true;
        }
    }
    QVERIFY(foundSet);
}

void TestValuePanel::buildsProtobufView()
{
    ProtobufViewDialog dialog(QStringLiteral("protobuf"),
                              QByteArray::fromHex("082a12026869"));
    auto *view = dialog.findChild<QPlainTextEdit *>(QStringLiteral("protobufView"));
    QVERIFY(view);
    QVERIFY(view->toPlainText().contains(QStringLiteral("1 (varint): 42")));
    QVERIFY(view->toPlainText().contains(QStringLiteral("2 (length-delimited): \"hi\"")));
}

void TestValuePanel::buildsSpecialTypeViews()
{
    FakeRedisClient client;
    BitmapViewDialog bitmap(QStringLiteral("bitmap"), QByteArray("\xff\x00", 2));
    auto *bitmapView = bitmap.findChild<QPlainTextEdit *>(QStringLiteral("bitmapView"));
    QVERIFY(bitmapView);
    QVERIFY(bitmapView->toPlainText().contains(QStringLiteral("FF")));

    HyperLogLogViewDialog hll(&client, QByteArrayLiteral("hll"));
    auto *hllStatus = hll.findChild<QLabel *>(QStringLiteral("hllStatus"));
    QVERIFY(hllStatus);
    QVERIFY(hllStatus->text().contains(QStringLiteral("3")));

    GeoSearchDialog geo(&client, QByteArrayLiteral("geo"));
    auto *search = geo.findChild<QPushButton *>(QStringLiteral("geoSearchButton"));
    auto *results = geo.findChild<QTableWidget *>(QStringLiteral("geoResults"));
    QVERIFY(search);
    QVERIFY(results);
    search->click();
    QCOMPARE(results->rowCount(), 1);
    QCOMPARE(results->item(0, 0)->text(), QStringLiteral("Palermo"));
}

void TestValuePanel::buildsStreamConsumerView()
{
    FakeRedisClient client;
    ValuePanel panel(&client);
    panel.loadKey(KeyInfo{QByteArrayLiteral("stream:test"), QStringLiteral("stream"), -1});
    auto *tabs = panel.findChild<QTabWidget *>(QStringLiteral("streamTabs"));
    auto *groups = panel.findChild<QTableWidget *>(QStringLiteral("streamGroupsTable"));
    auto *consumers = panel.findChild<QTableWidget *>(QStringLiteral("streamConsumersTable"));
    QVERIFY(tabs);
    QVERIFY(groups);
    QVERIFY(consumers);
    QTRY_COMPARE(groups->rowCount(), 1);
    groups->selectRow(0);
    QTRY_COMPARE(consumers->rowCount(), 1);
    QCOMPARE(consumers->item(0, 0)->text(), QStringLiteral("worker"));
}

void TestValuePanel::loadsLargeStringInChunks()
{
    FakeRedisClient client;
    ValuePanel panel(&client);
    panel.loadKey(KeyInfo{QByteArrayLiteral("large"), QStringLiteral("string"), -1});
    auto *edit = panel.findChild<QPlainTextEdit *>(QStringLiteral("stringEdit"));
    auto *next = panel.findChild<QPushButton *>(QStringLiteral("loadNextStringButton"));
    auto *save = panel.findChild<QPushButton *>(QStringLiteral("saveStringButton"));
    QVERIFY(edit);
    QVERIFY(next);
    QVERIFY(save);
    QTRY_COMPARE(edit->toPlainText().size(), 1024 * 1024);
    QVERIFY(!save->isEnabled());
    QTest::mouseClick(next, Qt::LeftButton);
    QTRY_COMPARE(edit->toPlainText().size(), 2 * 1024 * 1024);
    QTest::mouseClick(next, Qt::LeftButton);
    QTRY_COMPARE(edit->toPlainText().size(), int(kLargeSize));
    QVERIFY(save->isEnabled());
    QVERIFY(next->isHidden());
}

} // namespace KeyDis::Test
