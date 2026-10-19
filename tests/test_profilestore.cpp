// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "test_profilestore.h"

#include "core/conn/profilestore.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace KeyDis::Test {

void TestProfileStore::roundTripsProfiles()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("connections.json"));
    ProfileStore store(path);

    ConnectionProfile first;
    first.id = QStringLiteral("profile-1");
    first.name = QStringLiteral("local");
    first.group = QStringLiteral("Development");
    first.sentinel = true;
    first.sentinelMaster = QStringLiteral("mymaster");
    first.sentinelHost = QStringLiteral("127.0.0.1");
    first.sentinelPort = 26379;
    first.sshTunnel = true;
    first.sshHost = QStringLiteral("jump.example");
    first.sshPort = 2222;
    first.sshUser = QStringLiteral("deploy");
    first.sshKeyPath = QStringLiteral("/home/deploy/.ssh/id_ed25519");
    first.host = QStringLiteral("127.0.0.1");
    first.port = 6380;
    first.tls = true;
    first.tlsCaFile = QStringLiteral("/etc/keydis/ca.pem");
    first.tlsCertFile = QStringLiteral("/etc/keydis/client.pem");
    first.tlsKeyFile = QStringLiteral("/etc/keydis/client.key");
    first.tlsVerifyPeer = false;
    first.username = QStringLiteral("dev");
    first.db = 3;
    first.readOnly = true;

    ConnectionProfile second;
    second.name = QStringLiteral("prod");
    second.host = QStringLiteral("redis.internal");
    second.port = 6379;

    QString error;
    QVERIFY2(store.save({first, second}, &error), qPrintable(error));

    QVector<ConnectionProfile> loaded;
    QVERIFY2(store.load(&loaded, &error), qPrintable(error));
    QCOMPARE(loaded.size(), 2);
    QCOMPARE(loaded[0].id, first.id);
    QCOMPARE(loaded[0].name, first.name);
    QCOMPARE(loaded[0].group, first.group);
    QCOMPARE(loaded[0].sentinel, first.sentinel);
    QCOMPARE(loaded[0].sentinelMaster, first.sentinelMaster);
    QCOMPARE(loaded[0].sentinelHost, first.sentinelHost);
    QCOMPARE(loaded[0].sentinelPort, first.sentinelPort);
    QCOMPARE(loaded[0].sshTunnel, first.sshTunnel);
    QCOMPARE(loaded[0].sshHost, first.sshHost);
    QCOMPARE(loaded[0].sshPort, first.sshPort);
    QCOMPARE(loaded[0].sshUser, first.sshUser);
    QCOMPARE(loaded[0].sshKeyPath, first.sshKeyPath);
    QCOMPARE(loaded[0].host, first.host);
    QCOMPARE(loaded[0].port, first.port);
    QCOMPARE(loaded[0].tls, true);
    QCOMPARE(loaded[0].tlsCaFile, first.tlsCaFile);
    QCOMPARE(loaded[0].tlsCertFile, first.tlsCertFile);
    QCOMPARE(loaded[0].tlsKeyFile, first.tlsKeyFile);
    QCOMPARE(loaded[0].tlsVerifyPeer, false);
    QCOMPARE(loaded[0].username, first.username);
    QCOMPARE(loaded[0].db, first.db);
    QCOMPARE(loaded[0].readOnly, true);
    QCOMPARE(loaded[1].host, second.host);
}

void TestProfileStore::rejectsMalformedJson()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("connections.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{not json");
    file.close();

    ProfileStore store(path);
    QVector<ConnectionProfile> profiles;
    QString error;
    QVERIFY(!store.load(&profiles, &error));
    QVERIFY(!error.isEmpty());
}

} // namespace KeyDis::Test
