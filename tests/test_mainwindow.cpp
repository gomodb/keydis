// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "test_mainwindow.h"

#include "app/mainwindow.h"
#include "core/net/redisconnection.h"
#include "ui/models/keytablemodel.h"

#include <QEventLoop>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableView>
#include <QTreeView>
#include <QTableWidget>
#include <QTest>
#include <QTimer>

namespace KeyDis::Test {

namespace {

RespValue command(RedisConnection &connection, const QList<QByteArray> &args, QString *error)
{
    RespValue result;
    bool done = false;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    connection.sendCommand(args, [&](const RespValue &reply, const QString &replyError) {
        result = reply;
        if (error)
            *error = replyError;
        done = true;
        loop.quit();
    });
    timer.start(5000);
    if (!done)
        loop.exec();
    if (!done && error)
        *error = QStringLiteral("timeout");
    return result;
}

int rowForKey(QAbstractItemModel *model, const QByteArray &key)
{
    for (int row = 0; row < model->rowCount(); ++row) {
        if (model->index(row, 0).data(KeyTableModel::KeyRole).toByteArray() == key)
            return row;
    }
    return -1;
}

} // namespace

void TestMainWindow::browsesAndShowsValues()
{
    bool ok = false;
    const int port = qEnvironmentVariableIntValue("KEYDIS_REDIS_PORT", &ok);
    if (!ok || port <= 0)
        QSKIP("KEYDIS_REDIS_PORT is not set");

    const QByteArray stringKey("keydis:ui:string");
    const QByteArray hashKey("keydis:ui:hash");
    const QByteArray bigHashKey("keydis:ui:big-hash");

    {
        RedisConnection seed;
        seed.connectToHost(QStringLiteral("127.0.0.1"), static_cast<quint16>(port), false, {},
                           {}, 0);
        QTRY_VERIFY_WITH_TIMEOUT(seed.isReady(), 5000);
        QString error;
        command(seed, {QByteArrayLiteral("DEL"), stringKey, hashKey, bigHashKey}, &error);
        command(seed, {QByteArrayLiteral("SET"), stringKey, QByteArrayLiteral("hello")}, &error);
        command(seed, {QByteArrayLiteral("HSET"), hashKey, QByteArrayLiteral("field"),
                       QByteArrayLiteral("value")}, &error);
        QList<QByteArray> bigHash{QByteArrayLiteral("HSET"), bigHashKey};
        for (int index = 0; index < 600; ++index) {
            bigHash.append(QByteArrayLiteral("field-") + QByteArray::number(index));
            bigHash.append(QByteArrayLiteral("value-") + QByteArray::number(index));
        }
        command(seed, bigHash, &error);
        command(seed, {QByteArrayLiteral("SELECT"), QByteArrayLiteral("1")}, &error);
        command(seed, {QByteArrayLiteral("SET"), QByteArrayLiteral("keydis:ui:db1"),
                       QByteArrayLiteral("db1-value")}, &error);
        command(seed, {QByteArrayLiteral("SELECT"), QByteArrayLiteral("0")}, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        seed.disconnectFromHost();
    }

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.show();

    auto *host = window.findChild<QLineEdit *>(QStringLiteral("hostEdit"));
    auto *portSpin = window.findChild<QSpinBox *>(QStringLiteral("portSpin"));
    auto *filter = window.findChild<QLineEdit *>(QStringLiteral("filterEdit"));
    auto *connectButton = window.findChild<QPushButton *>(QStringLiteral("connectButton"));
    auto *table = window.findChild<QTableView *>(QStringLiteral("keyTable"));
    auto *tree = window.findChild<QTreeView *>(QStringLiteral("keyTree"));
    auto *viewMode = window.findChild<QComboBox *>(QStringLiteral("viewMode"));
    auto *dbCombo = window.findChild<QComboBox *>(QStringLiteral("dbCombo"));
    auto *stack = window.findChild<QStackedWidget *>(QStringLiteral("valueStack"));
    auto *stringEdit = window.findChild<QPlainTextEdit *>(QStringLiteral("stringEdit"));
    auto *hashTable = window.findChild<QTableWidget *>(QStringLiteral("hashTable"));
    auto *hashMoreButton = window.findChild<QPushButton *>(QStringLiteral("hashMoreButton"));
    auto *keyLabel = window.findChild<QLabel *>(QStringLiteral("keyLabel"));
    auto *saveButton = window.findChild<QPushButton *>(QStringLiteral("saveStringButton"));
    auto *readOnlyCheck = window.findChild<QCheckBox *>(QStringLiteral("readOnlyCheck"));

    QVERIFY(host);
    QVERIFY(portSpin);
    QVERIFY(filter);
    QVERIFY(connectButton);
    QVERIFY(table);
    QVERIFY(tree);
    QVERIFY(viewMode);
    QVERIFY(dbCombo);
    QVERIFY(stack);
    QVERIFY(stringEdit);
    QVERIFY(hashTable);
    QVERIFY(hashMoreButton);
    QVERIFY(keyLabel);
    QVERIFY(saveButton);
    QVERIFY(readOnlyCheck);

    host->setText(QStringLiteral("127.0.0.1"));
    portSpin->setValue(port);
    filter->setText(QStringLiteral("keydis:ui:*"));
    QTest::mouseClick(connectButton, Qt::LeftButton);

    QTRY_VERIFY_WITH_TIMEOUT(table->model()->rowCount() >= 3, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(tree->model()->rowCount() > 0, 5000);
    QCOMPARE(dbCombo->currentData().toInt(), 0);
    viewMode->setCurrentIndex(1);
    const int stringRow = rowForKey(table->model(), stringKey);
    const int hashRow = rowForKey(table->model(), hashKey);
    const int bigHashRow = rowForKey(table->model(), bigHashKey);
    QVERIFY(stringRow >= 0);
    QVERIFY(hashRow >= 0);
    QVERIFY(bigHashRow >= 0);

    table->selectRow(stringRow);
    QTRY_VERIFY_WITH_TIMEOUT(stringEdit->toPlainText() == QStringLiteral("hello"), 5000);
    QVERIFY(keyLabel->text().contains(QString::fromUtf8(stringKey)));

    readOnlyCheck->setChecked(true);
    QVERIFY(!saveButton->isEnabled());
    readOnlyCheck->setChecked(false);
    QVERIFY(saveButton->isEnabled());

    stringEdit->setPlainText(QStringLiteral("hello2"));
    QTest::mouseClick(saveButton, Qt::LeftButton);
    QTest::qWait(200);

    table->selectRow(hashRow);
    QTRY_VERIFY_WITH_TIMEOUT(hashTable->rowCount() == 1, 5000);
    QCOMPARE(hashTable->item(0, 0)->text(), QStringLiteral("field"));
    QCOMPARE(hashTable->item(0, 1)->text(), QStringLiteral("value"));
    QVERIFY(keyLabel->text().contains(QString::fromUtf8(hashKey)));

    const QString capturePath = qEnvironmentVariable("KEYDIS_CAPTURE");
    if (!capturePath.isEmpty())
        window.grab().save(capturePath);

    hashTable->item(0, 1)->setText(QStringLiteral("value2"));
    QTest::qWait(200);

    table->selectRow(bigHashRow);
    QTRY_VERIFY_WITH_TIMEOUT(hashTable->rowCount() == 500, 5000);
    QVERIFY(hashMoreButton->isEnabled());
    QTest::mouseClick(hashMoreButton, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(hashTable->rowCount() == 600, 5000);

    dbCombo->setCurrentIndex(1);
    QTRY_COMPARE_WITH_TIMEOUT(dbCombo->currentData().toInt(), 1, 1000);
    QTRY_VERIFY_WITH_TIMEOUT(
        rowForKey(table->model(), QByteArrayLiteral("keydis:ui:db1")) >= 0, 5000);

    {
        RedisConnection cleanup;
        cleanup.connectToHost(QStringLiteral("127.0.0.1"), static_cast<quint16>(port), false, {},
                              {}, 0);
        QTRY_VERIFY_WITH_TIMEOUT(cleanup.isReady(), 5000);
        QString error;
        const RespValue stringValue =
            command(cleanup, {QByteArrayLiteral("GET"), stringKey}, &error);
        QCOMPARE(stringValue.scalar, QByteArray("hello2"));
        const RespValue hashValue =
            command(cleanup, {QByteArrayLiteral("HGET"), hashKey, QByteArrayLiteral("field")},
                    &error);
        QCOMPARE(hashValue.scalar, QByteArray("value2"));
        command(cleanup, {QByteArrayLiteral("DEL"), stringKey, hashKey, bigHashKey}, &error);
        command(cleanup, {QByteArrayLiteral("SELECT"), QByteArrayLiteral("1")}, &error);
        command(cleanup, {QByteArrayLiteral("DEL"), QByteArrayLiteral("keydis:ui:db1")}, &error);
        command(cleanup, {QByteArrayLiteral("SELECT"), QByteArrayLiteral("0")}, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }
}

} // namespace KeyDis::Test
