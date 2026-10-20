// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "test_mainwindow.h"

#include "app/mainwindow.h"
#include "app/sessionhost.h"
#include "core/app_paths.h"
#include "core/conn/profilestore.h"
#include "core/net/clusterclient.h"
#include "core/net/redisconnection.h"
#include "ui/models/keytablemodel.h"

#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QApplication>
#include <QCheckBox>
#include <QAction>
#include <QDialog>
#include <QDockWidget>
#include <QComboBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableView>
#include <QTreeView>
#include <QTreeWidget>
#include <QTableWidget>
#include <QTest>
#include <QToolBar>
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
    const QByteArray batchKeyA("keydis:ui:batch-a");
    const QByteArray binaryKey("keydis:ui:binary");
    const QByteArray gzipKey("keydis:ui:gzip");
    const QByteArray msgPackKey("keydis:ui:msgpack");
    const QByteArray largeKey("keydis:ui:large");
    const QByteArray bitmapKey("keydis:ui:bitmap");
    const QByteArray hllKey("keydis:ui:hll");
    const QByteArray geoKey("keydis:ui:geo");
    const QByteArray protobufKey("keydis:ui:protobuf");
    const QByteArray batchKeyB("keydis:ui:batch-b");

    {
        RedisConnection seed;
        seed.connectToHost(QStringLiteral("127.0.0.1"), static_cast<quint16>(port), false, {},
                           {}, 0);
        QTRY_VERIFY_WITH_TIMEOUT(seed.isReady(), 5000);
        QString error;
        command(seed, {QByteArrayLiteral("DEL"), stringKey, hashKey, bigHashKey, binaryKey,
                       gzipKey, msgPackKey, largeKey, bitmapKey, hllKey}, &error);
        command(seed, {QByteArrayLiteral("SET"), binaryKey,
                       QByteArray("\x00\x01\xff", 3)}, &error);
        command(seed, {QByteArrayLiteral("SET"), gzipKey,
                       QByteArray::fromHex("1f8b08000000000002ffcb48cdc9c9070086a6103605000000")},
                &error);
        command(seed, {QByteArrayLiteral("SET"), msgPackKey,
                       QByteArray::fromHex("83a16101a16293c3a178c0a16302")}, &error);
        command(seed, {QByteArrayLiteral("SET"), largeKey,
                       QByteArray(1200 * 1024, 'x')}, &error);
        command(seed, {QByteArrayLiteral("SET"), bitmapKey,
                       QByteArray("\xff\x00", 2)}, &error);
        command(seed, {QByteArrayLiteral("PFADD"), hllKey, QByteArrayLiteral("a"),
                       QByteArrayLiteral("b"), QByteArrayLiteral("c")}, &error);
        command(seed, {QByteArrayLiteral("SET"), stringKey, QByteArrayLiteral("hello")}, &error);
        command(seed, {QByteArrayLiteral("HSET"), hashKey, QByteArrayLiteral("field"),
                       QByteArrayLiteral("value")}, &error);
        command(seed, {QByteArrayLiteral("HEXPIRE"), hashKey, QByteArrayLiteral("120"),
                       QByteArrayLiteral("FIELDS"), QByteArrayLiteral("1"),
                       QByteArrayLiteral("field")}, &error);
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
    auto *redoButton = window.findChild<QPushButton *>(QStringLiteral("redoButton"));
    auto *applyButton = window.findChild<QPushButton *>(QStringLiteral("applyButton"));
    auto *discardPendingButton =
        window.findChild<QPushButton *>(QStringLiteral("discardPendingButton"));
    auto *valueFilter = window.findChild<QLineEdit *>(QStringLiteral("valueFilter"));
    auto *searchAllButton =
        window.findChild<QPushButton *>(QStringLiteral("searchAllButton"));
    auto *hexButton = window.findChild<QPushButton *>(QStringLiteral("hexViewButton"));
    auto *editBytesButton =
        window.findChild<QPushButton *>(QStringLiteral("editBytesButton"));
    auto *decodeButton = window.findChild<QPushButton *>(QStringLiteral("decodeButton"));
    auto *msgPackButton = window.findChild<QPushButton *>(QStringLiteral("msgPackButton"));
    auto *loadFullStringButton =
        window.findChild<QPushButton *>(QStringLiteral("loadFullStringButton"));
    auto *bitmapButton = window.findChild<QPushButton *>(QStringLiteral("bitmapButton"));
    auto *hllButton = window.findChild<QPushButton *>(QStringLiteral("hllButton"));
    auto *geoButton = window.findChild<QPushButton *>(QStringLiteral("geoButton"));
    auto *protobufButton = window.findChild<QPushButton *>(QStringLiteral("protobufButton"));
    auto *keyLabel = window.findChild<QLabel *>(QStringLiteral("keyLabel"));
    auto *saveButton = window.findChild<QPushButton *>(QStringLiteral("saveStringButton"));
    auto *discardStringButton =
        window.findChild<QPushButton *>(QStringLiteral("discardStringButton"));
    auto *dirtyLabel = window.findChild<QLabel *>(QStringLiteral("dirtyLabel"));
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
    QVERIFY(redoButton);
    QVERIFY(applyButton);
    QVERIFY(discardPendingButton);
    QVERIFY(valueFilter);
    QVERIFY(searchAllButton);
    QVERIFY(hexButton);
    QVERIFY(editBytesButton);
    QVERIFY(decodeButton);
    QVERIFY(msgPackButton);
    QVERIFY(loadFullStringButton);
    QVERIFY(bitmapButton);
    QVERIFY(hllButton);
    QVERIFY(geoButton);
    QVERIFY(protobufButton);
    QVERIFY(keyLabel);
    QVERIFY(saveButton);
    QVERIFY(discardStringButton);
    QVERIFY(dirtyLabel);
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
    QVERIFY(!dirtyLabel->isHidden());
    QVERIFY(discardStringButton->isEnabled());
    QTest::mouseClick(discardStringButton, Qt::LeftButton);
    QCOMPARE(stringEdit->toPlainText(), QStringLiteral("hello"));
    QVERIFY(dirtyLabel->isHidden());
    stringEdit->setPlainText(QStringLiteral("hello2"));
    QTest::mouseClick(saveButton, Qt::LeftButton);
    QTest::qWait(200);

    table->selectRow(hashRow);
    QTRY_VERIFY_WITH_TIMEOUT(hashTable->rowCount() == 1, 5000);
    QCOMPARE(hashTable->item(0, 0)->text(), QStringLiteral("field"));
    QCOMPARE(hashTable->item(0, 1)->text(), QStringLiteral("value"));
    QTRY_VERIFY_WITH_TIMEOUT(hashTable->item(0, 2)->text() != QStringLiteral("-"), 5000);
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
    QVERIFY(applyButton->isEnabled());
    QTest::mouseClick(applyButton, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(!applyButton->isEnabled(), 5000);

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
    QVERIFY(applyButton->isEnabled());
    QTest::mouseClick(applyButton, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(!applyButton->isEnabled(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(undoButton->isEnabled(), 2000);
    QTest::mouseClick(undoButton, Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(hashTable->item(0, 1)->text(), QStringLiteral("value2"), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(redoButton->isEnabled(), 2000);
    QTest::mouseClick(redoButton, Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(hashTable->item(0, 1)->text(), QStringLiteral("batch-value"), 5000);
    QTest::mouseClick(undoButton, Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(hashTable->item(0, 1)->text(), QStringLiteral("value2"), 5000);

    hashTable->item(0, 1)->setText(QStringLiteral("discard-me"));
    QVERIFY(discardPendingButton->isEnabled());
    QTest::mouseClick(discardPendingButton, Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(hashTable->item(0, 1)->text(), QStringLiteral("value2"), 5000);
    QVERIFY(!applyButton->isEnabled());

    table->selectRow(bigHashRow);
    QTRY_VERIFY_WITH_TIMEOUT(hashTable->rowCount() >= 500, 5000);
    if (hashTable->rowCount() < 600) {
        QVERIFY(hashMoreButton->isEnabled());
        QTest::mouseClick(hashMoreButton, Qt::LeftButton);
    }
    QTRY_COMPARE_WITH_TIMEOUT(hashTable->rowCount(), 600, 5000);
    QTimer::singleShot(50, qApp, [] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (box)
            box->button(QMessageBox::Yes)->click();
    });
    QTest::mouseClick(searchAllButton, Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(hashTable->rowCount(), 600, 5000);
    QVERIFY(!valueFilter->isHidden());
    valueFilter->setText(QStringLiteral("field-599"));
    QTRY_VERIFY_WITH_TIMEOUT(
        [hashTable] {
            int visible = 0;
            for (int row = 0; row < hashTable->rowCount(); ++row) {
                if (!hashTable->isRowHidden(row))
                    ++visible;
            }
            return visible == 1;
        }(),
        2000);
    valueFilter->clear();

    bool hexDialogSeen = false;
    bool hexContentOk = false;
    QTimer::singleShot(50, qApp, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog)
            return;
        hexDialogSeen = true;
        auto *view = dialog->findChild<QPlainTextEdit *>();
        if (view && view->toPlainText().contains(QStringLiteral("68 65 6C 6C 6F 32")))
            hexContentOk = true;
        dialog->accept();
    });
    QTest::mouseClick(hexButton, Qt::LeftButton);
    QVERIFY(hexDialogSeen);
    QVERIFY(hexContentOk);

    auto countWindows = [] {
        int count = 0;
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (qobject_cast<MainWindow *>(widget))
                ++count;
        }
        return count;
    };
    const int windowsBefore = countWindows();
    QVERIFY(QMetaObject::invokeMethod(&window, "newWindow", Qt::DirectConnection));
    QTRY_VERIFY_WITH_TIMEOUT(countWindows() > windowsBefore, 5000);
    MainWindow *extraWindow = nullptr;
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (auto *candidate = qobject_cast<MainWindow *>(widget);
            candidate && candidate != &window) {
            extraWindow = candidate;
            break;
        }
    }
    QVERIFY(extraWindow);
    extraWindow->close();
    QTRY_COMPARE_WITH_TIMEOUT(countWindows(), windowsBefore, 5000);

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
        command(cleanup, {QByteArrayLiteral("DEL"), stringKey, hashKey, bigHashKey, batchKeyA,
                          batchKeyB, binaryKey, gzipKey, msgPackKey, largeKey, bitmapKey,
                       hllKey}, &error);
        command(cleanup, {QByteArrayLiteral("SELECT"), QByteArrayLiteral("1")}, &error);
        command(cleanup, {QByteArrayLiteral("DEL"), QByteArrayLiteral("keydis:ui:db1")}, &error);
        command(cleanup, {QByteArrayLiteral("SELECT"), QByteArrayLiteral("0")}, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }
}

void TestMainWindow::hostsMultipleSessionTabs()
{
    SessionHost host;
    host.show();
    auto *first = host.addSession();
    QVERIFY(first);
    auto *second = host.addSession();
    QVERIFY(second);
    QVERIFY(first != second);
    QCOMPARE(host.sessionCount(), 2);
    auto *tabs = host.findChild<QTabWidget *>(QStringLiteral("sessionTabs"));
    QVERIFY(tabs);
    QCOMPARE(tabs->count(), 2);
    auto *splitter = host.findChild<QSplitter *>(QStringLiteral("sessionSplitter"));
    QVERIFY(splitter);
    QCOMPARE(splitter->handleWidth(), 8);
    QCOMPARE(host.centralWidget(), splitter);
    auto *connectionToolBar =
        host.findChild<QToolBar *>(QStringLiteral("toolBarConnection"));
    auto *newTabAction = host.findChild<QAction *>(QStringLiteral("newSessionAction"));
    auto *refreshAction = host.findChild<QAction *>(QStringLiteral("refreshKeysAction"));
    QVERIFY(connectionToolBar);
    QVERIFY(newTabAction);
    QVERIFY(refreshAction);
    QVERIFY(connectionToolBar->actions().contains(newTabAction));
    QVERIFY(connectionToolBar->actions().contains(refreshAction));
    tabs->setCurrentWidget(first);
    auto *dock = first->findChild<QDockWidget *>(QStringLiteral("connectionsDock"));
    auto *dockAction = host.findChild<QAction *>(QStringLiteral("connectionsDockAction"));
    QVERIFY(dock);
    QVERIFY(dockAction);
    QVERIFY(dock->minimumWidth() <= 260);
    QVERIFY(dock->minimumSizeHint().width() <= 300);
    QVERIFY(!dock->isHidden());
    dock->close();
    QVERIFY(dock->isHidden());
    QVERIFY(!dockAction->isChecked());
    dockAction->trigger();
    QVERIFY(!dock->isHidden());
    QVERIFY(dockAction->isChecked());

    QVERIFY(QMetaObject::invokeMethod(first, "newWindow", Qt::DirectConnection));
    QTRY_COMPARE_WITH_TIMEOUT(host.sessionCount(), 3, 5000);

    tabs->tabCloseRequested(0);
    QTRY_COMPARE_WITH_TIMEOUT(host.sessionCount(), 2, 5000);

    QVERIFY(QMetaObject::invokeMethod(&host, "splitView", Qt::DirectConnection));
    QTRY_COMPARE_WITH_TIMEOUT(host.sessionCount(), 3, 5000);
    auto *secondary = host.findChild<QTabWidget *>(QStringLiteral("sessionTabsSecondary"));
    QVERIFY(secondary);
    QCOMPARE(secondary->count(), 1);
    QVERIFY(QMetaObject::invokeMethod(&host, "closeSplit", Qt::DirectConnection));
    QTRY_COMPARE_WITH_TIMEOUT(host.sessionCount(), 2, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(
        !host.findChild<QTabWidget *>(QStringLiteral("sessionTabsSecondary")), 5000);
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
