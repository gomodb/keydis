// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "test_valuepanel.h"

#include "core/net/redisclient.h"
#include "core/keyspace/keyinfo.h"
#include "ui/values/valuepanel.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTest>

namespace KeyDis::Test {

namespace {

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
        if (!args.isEmpty() && args.first() == QByteArrayLiteral("JSON.GET")) {
            reply.type = RespType::BulkString;
            reply.scalar = R"({"name":"Alice","tags":["a","b"],"active":true})";
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

} // namespace KeyDis::Test
