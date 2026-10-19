// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "test_clusterclient.h"
#include "test_commandsafety.h"
#include "test_commandencoder.h"
#include "test_keyscanner.h"
#include "test_language.h"
#include "test_logger.h"
#include "test_namespacetreemodel.h"
#include "test_profilestore.h"
#include "test_redisconnection.h"
#include "test_redisinfo.h"
#include "test_redisintegration.h"
#include "test_sshtunnel.h"
#include "test_resp.h"

#include <QCoreApplication>
#include <QTest>

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    int status = 0;

    KeyDis::Test::TestResp resp;
    status |= QTest::qExec(&resp, argc, argv);

    KeyDis::Test::TestCommandEncoder encoder;
    status |= QTest::qExec(&encoder, argc, argv);

    KeyDis::Test::TestCommandSafety safety;
    status |= QTest::qExec(&safety, argc, argv);

    KeyDis::Test::TestClusterClient cluster;
    status |= QTest::qExec(&cluster, argc, argv);

    KeyDis::Test::TestLanguage language;
    status |= QTest::qExec(&language, argc, argv);

    KeyDis::Test::TestNamespaceTreeModel treeModel;
    status |= QTest::qExec(&treeModel, argc, argv);

    KeyDis::Test::TestProfileStore profileStore;
    status |= QTest::qExec(&profileStore, argc, argv);

    KeyDis::Test::TestKeyScanner scanner;
    status |= QTest::qExec(&scanner, argc, argv);

    KeyDis::Test::TestLogger logger;
    status |= QTest::qExec(&logger, argc, argv);

    KeyDis::Test::TestRedisConnection connection;
    status |= QTest::qExec(&connection, argc, argv);

    KeyDis::Test::TestRedisInfo redisInfo;
    status |= QTest::qExec(&redisInfo, argc, argv);

    KeyDis::Test::TestRedisIntegration integration;
    status |= QTest::qExec(&integration, argc, argv);

    KeyDis::Test::TestSshTunnel sshTunnel;
    status |= QTest::qExec(&sshTunnel, argc, argv);

    return status;
}
