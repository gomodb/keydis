// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "test_mainwindow.h"

#include "app/mainwindow.h"
#include "core/app_paths.h"
#include "core/conn/profilestore.h"
#include "core/net/clusterclient.h"
#include "core/net/redisconnection.h"
#include "ui/models/keytablemodel.h"

#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QComboBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableView>
#include <QTreeView>
#include <QTreeWidget>
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

void TestMainWindow::initTestCase()
{
    if (qEnvironmentVariableIsSet("KEYDIS_TEST_CONFIG_RESET"))
        QFile::remove(KeyDis::AppPaths::configFile());
}

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
    auto *hashBatchButton = window.findChild<QPushButton *>(QStringLiteral("hashBatchEditButton"));
    auto *undoButton = window.findChild<QPushButton *>(QStringLiteral("undoButton"));
    auto *keyLabel = window.findChild<QLabel *>(QStringLiteral("keyLabel"));
    auto *saveButton = window.findChild<QPushButton *>(QStringLiteral("saveStringButton"));
    auto *readOnlyCheck = window.findChild<QCheckBox *>(QStringLiteral("readOnlyCheck"));
    auto *profileList = window.findChild<QTreeWidget *>(QStringLiteral("profileList"));
    auto *profileConnect = window.findChild<QPushButton *>(QStringLiteral("profileConnectButton"));
    auto *profileEmpty = window.findChild<QLabel *>(QStringLiteral("profileEmptyLabel"));

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
    QVERIFY(hashBatchButton);
    QVERIFY(undoButton);
    QVERIFY(keyLabel);
    QVERIFY(saveButton);
    QVERIFY(readOnlyCheck);
    QVERIFY(profileList);
    QVERIFY(profileConnect);
    QVERIFY(profileEmpty);
    QVERIFY(!profileConnect->isEnabled());
    QVERIFY(profileEmpty->isVisible());

    host->setText(QStringLiteral("127.0.0.1"));
    portSpin->setValue(port);
    filter->setText(QStringLiteral("keydis:ui:*"));
    connectButton->click();

    QTRY_VERIFY_WITH_TIMEOUT(table->model()->rowCount() >= 3, 10000);
    QCOMPARE(profileList->topLevelItemCount(), 0);
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

    const QString dashboardCapture = qEnvironmentVariable("KEYDIS_CAPTURE_DASHBOARD");
    if (!dashboardCapture.isEmpty()) {
        window.showTab(QStringLiteral("dashboard"));
        QTest::qWait(150);
        window.grab().save(dashboardCapture);
    }
    const QString capturePath = qEnvironmentVariable("KEYDIS_CAPTURE");
    if (!capturePath.isEmpty()) {
        window.showTab(QStringLiteral("browser"));
        viewMode->setCurrentIndex(0);
        QTest::qWait(150);
        window.grab().save(capturePath);
        viewMode->setCurrentIndex(1);
    }

    hashTable->item(0, 1)->setText(QStringLiteral("value2"));
    QTest::qWait(200);

    hashTable->selectRow(0);
    QTimer::singleShot(50, qApp, [] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        auto *table = dialog->findChild<QTableWidget *>(QStringLiteral("batchEditTable"));
        QVERIFY(table);
        table->item(0, 1)->setText(QStringLiteral("batch-value"));
        dialog->accept();
    });
    QTest::mouseClick(hashBatchButton, Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(hashTable->item(0, 1)->text(), QStringLiteral("batch-value"), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(undoButton->isEnabled(), 2000);
    QTest::mouseClick(undoButton, Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(hashTable->item(0, 1)->text(), QStringLiteral("value2"), 5000);

    table->selectRow(bigHashRow);
    QTRY_VERIFY_WITH_TIMEOUT(hashTable->rowCount() >= 500, 5000);
    if (hashTable->rowCount() < 600) {
        QVERIFY(hashMoreButton->isEnabled());
        QTest::mouseClick(hashMoreButton, Qt::LeftButton);
    }
    QTRY_COMPARE_WITH_TIMEOUT(hashTable->rowCount(), 600, 5000);

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

void TestMainWindow::showsGroupedProfiles()
{
    QFile::remove(KeyDis::AppPaths::configFile());
    QVector<ConnectionProfile> profiles;
    ConnectionProfile first;
    first.id = QStringLiteral("alpha");
    first.name = QStringLiteral("Alpha");
    first.group = QStringLiteral("Group A");
    first.color = QStringLiteral("#ff0000");
    first.host = QStringLiteral("127.0.0.1");
    profiles.append(first);
    ConnectionProfile second;
    second.id = QStringLiteral("beta");
    second.name = QStringLiteral("Beta");
    second.group = QStringLiteral("Group B");
    second.host = QStringLiteral("127.0.0.1");
    profiles.append(second);
    ProfileStore store;
    QString error;
    QVERIFY2(store.save(profiles, &error), qPrintable(error));

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    auto *tree = window.findChild<QTreeWidget *>(QStringLiteral("profileList"));
    QVERIFY(tree);
    QCOMPARE(tree->topLevelItemCount(), 2);
    QTreeWidgetItem *groupA = tree->topLevelItem(0);
    QTreeWidgetItem *groupB = tree->topLevelItem(1);
    QCOMPARE(groupA->text(0), QStringLiteral("Group A"));
    QCOMPARE(groupB->text(0), QStringLiteral("Group B"));
    QCOMPARE(groupA->childCount(), 1);
    QCOMPARE(groupA->child(0)->text(0), QStringLiteral("Alpha"));
    QVERIFY(!groupA->child(0)->icon(0).isNull());
    groupA->setExpanded(false);
    QVERIFY(!groupA->isExpanded());
    QFile::remove(KeyDis::AppPaths::configFile());
}

void TestMainWindow::browsesCluster()
{
    bool ok = false;
    const int port = qEnvironmentVariableIntValue("KEYDIS_CLUSTER_PORT", &ok);
    if (!ok || port <= 0)
        QSKIP("KEYDIS_CLUSTER_PORT is not set");

    const QByteArray keyA("keydis:cluster:ui:a");
    const QByteArray keyB("keydis:cluster:ui:b");
    const QByteArray keyC("keydis:cluster:ui:c");

    {
        ClusterClient seed;
        bool ready = false;
        QObject::connect(&seed, &RedisClient::stateChanged, &seed,
                         [&](const QString &state) {
            if (state == QLatin1String("Ready"))
                ready = true;
        });
        seed.connectToHost(QStringLiteral("127.0.0.1"), static_cast<quint16>(port), false, {},
                           {}, 0);
        QTRY_VERIFY_WITH_TIMEOUT(ready, 15000);
        bool done = false;
        auto run = [&](const QList<QByteArray> &args) {
            done = false;
            seed.sendCommand(args, [&](const RespValue &, const QString &) { done = true; });
            QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        };
        run({QByteArrayLiteral("DEL"), keyA, keyB, keyC});
        run({QByteArrayLiteral("SET"), keyA, QByteArrayLiteral("a")});
        run({QByteArrayLiteral("SET"), keyB, QByteArrayLiteral("b")});
        run({QByteArrayLiteral("SET"), keyC, QByteArrayLiteral("c")});
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
    auto *dbCombo = window.findChild<QComboBox *>(QStringLiteral("dbCombo"));
    auto *clusterCheck = window.findChild<QCheckBox *>(QStringLiteral("clusterCheck"));
    auto *clusterTable = window.findChild<QTableWidget *>(QStringLiteral("clusterTable"));
    QVERIFY(host);
    QVERIFY(portSpin);
    QVERIFY(filter);
    QVERIFY(connectButton);
    QVERIFY(table);
    QVERIFY(tree);
    QVERIFY(dbCombo);
    QVERIFY(clusterCheck);
    QVERIFY(clusterTable);

    host->setText(QStringLiteral("127.0.0.1"));
    portSpin->setValue(port);
    filter->setText(QStringLiteral("keydis:cluster:ui:*"));
    clusterCheck->setChecked(true);
    connectButton->click();

    QTRY_VERIFY_WITH_TIMEOUT(table->model()->rowCount() >= 3, 15000);
    QTRY_VERIFY_WITH_TIMEOUT(tree->model()->rowCount() > 0, 5000);
    QVERIFY(!dbCombo->isEnabled());
    QVERIFY(rowForKey(table->model(), keyA) >= 0);
    QVERIFY(rowForKey(table->model(), keyB) >= 0);
    QVERIFY(rowForKey(table->model(), keyC) >= 0);
    QTRY_VERIFY_WITH_TIMEOUT(clusterTable->rowCount() >= 3, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(
        [clusterTable] {
            for (int row = 0; row < clusterTable->rowCount(); ++row) {
                if (clusterTable->item(row, 6)
                    && clusterTable->item(row, 6)->text() != QStringLiteral("-"))
                    return true;
            }
            return false;
        }(),
        10000);

    table->selectRow(rowForKey(table->model(), keyA));
    auto *stringEdit = window.findChild<QPlainTextEdit *>(QStringLiteral("stringEdit"));
    QVERIFY(stringEdit);
    QTRY_VERIFY_WITH_TIMEOUT(stringEdit->toPlainText() == QStringLiteral("a"), 5000);

    {
        ClusterClient cleanup;
        bool ready = false;
        QObject::connect(&cleanup, &RedisClient::stateChanged, &cleanup,
                         [&](const QString &state) {
            if (state == QLatin1String("Ready"))
                ready = true;
        });
        cleanup.connectToHost(QStringLiteral("127.0.0.1"), static_cast<quint16>(port), false, {},
                              {}, 0);
        QTRY_VERIFY_WITH_TIMEOUT(ready, 15000);
        bool done = false;
        cleanup.sendCommand({QByteArrayLiteral("DEL"), keyA, keyB, keyC},
                            [&](const RespValue &, const QString &) { done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    }
}

} // namespace KeyDis::Test
