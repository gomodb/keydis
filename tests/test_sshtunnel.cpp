// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "test_sshtunnel.h"

#include "core/net/sshtunnel.h"

#include <QFileInfo>
#include <QStandardPaths>
#include <QTest>

namespace KeyDis::Test {

void TestSshTunnel::failsForUnreachableHost()
{
    if (QStandardPaths::findExecutable(QStringLiteral("ssh")).isEmpty())
        QSKIP("ssh executable not available");

    SshTunnel tunnel;
    bool failed = false;
    QString error;
    QObject::connect(&tunnel, &SshTunnel::failed, &tunnel, [&](const QString &message) {
        failed = true;
        error = message;
    });
    tunnel.start(QStringLiteral("127.0.0.1"), 1, {}, {}, {}, QStringLiteral("127.0.0.1"), 6379);
    QTRY_VERIFY_WITH_TIMEOUT(failed, 15000);
    QVERIFY(!error.isEmpty());
    tunnel.stop();
}

} // namespace KeyDis::Test
