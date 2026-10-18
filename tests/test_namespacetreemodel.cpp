// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "test_namespacetreemodel.h"

#include "ui/models/namespacetreemodel.h"

#include <QTest>

namespace KeyDis::Test {

namespace {

KeyInfo key(const char *value)
{
    KeyInfo info;
    info.key = QByteArray(value);
    info.type = QStringLiteral("string");
    info.ttlMs = -1;
    return info;
}

QModelIndex childByName(QAbstractItemModel *model, const QModelIndex &parent, const QString &name)
{
    for (int row = 0; row < model->rowCount(parent); ++row) {
        const QModelIndex index = model->index(row, 0, parent);
        if (index.data(Qt::DisplayRole).toString() == name)
            return index;
    }
    return {};
}

} // namespace

void TestNamespaceTreeModel::buildsNamespacesAndCounts()
{
    NamespaceTreeModel model;
    model.addKeys({key("demo:user:1"), key("demo:user:2"), key("demo:session:a"),
                   key("other"), key("a:b:c")});

    QCOMPARE(model.loadedKeyCount(), 5);
    QCOMPARE(model.rowCount(), 3);

    const QModelIndex demo = childByName(&model, {}, QStringLiteral("demo"));
    QVERIFY(demo.isValid());
    QCOMPARE(demo.siblingAtColumn(NamespaceTreeModel::TypeColumn).data().toString(),
             QStringLiteral("3"));

    const QModelIndex user = childByName(&model, demo, QStringLiteral("user"));
    QVERIFY(user.isValid());
    QCOMPARE(user.siblingAtColumn(NamespaceTreeModel::TypeColumn).data().toString(),
             QStringLiteral("2"));

    const QModelIndex one = childByName(&model, user, QStringLiteral("1"));
    QVERIFY(one.isValid());
    QVERIFY(model.isKey(one));
    QCOMPARE(model.keyInfo(one).key, QByteArray("demo:user:1"));
    QVERIFY(!model.isKey(user));

    const QModelIndex other = childByName(&model, {}, QStringLiteral("other"));
    QVERIFY(other.isValid());
    QVERIFY(model.isKey(other));
}

void TestNamespaceTreeModel::removesKeysAndUpdatesCounts()
{
    NamespaceTreeModel model;
    model.addKeys({key("demo:user:1"), key("demo:user:2"), key("demo:session:a")});
    model.removeKey(QByteArray("demo:user:1"));

    QCOMPARE(model.loadedKeyCount(), 2);
    const QModelIndex demo = childByName(&model, {}, QStringLiteral("demo"));
    QVERIFY(demo.isValid());
    QCOMPARE(demo.siblingAtColumn(NamespaceTreeModel::TypeColumn).data().toString(),
             QStringLiteral("2"));
    const QModelIndex user = childByName(&model, demo, QStringLiteral("user"));
    QVERIFY(user.isValid());
    QCOMPARE(user.siblingAtColumn(NamespaceTreeModel::TypeColumn).data().toString(),
             QStringLiteral("1"));
}

} // namespace KeyDis::Test
