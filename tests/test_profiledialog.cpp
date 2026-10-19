// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "test_profiledialog.h"

#include "core/conn/connectionprofile.h"
#include "ui/dialogs/profiledialog.h"

#include <QLabel>
#include <QPushButton>
#include <QTest>

namespace KeyDis::Test {

void TestProfileDialog::testsDirectConnection()
{
    bool ok = false;
    const int port = qEnvironmentVariableIntValue("KEYDIS_REDIS_PORT", &ok);
    if (!ok || port <= 0)
        QSKIP("KEYDIS_REDIS_PORT is not set");

    ConnectionProfile profile;
    profile.name = QStringLiteral("test");
    profile.host = QStringLiteral("127.0.0.1");
    profile.port = static_cast<quint16>(port);
    ProfileDialog dialog(profile);
    auto *button = dialog.findChild<QPushButton *>(QStringLiteral("testConnectionButton"));
    auto *status = dialog.findChild<QLabel *>(QStringLiteral("testConnectionStatus"));
    QVERIFY(button);
    QVERIFY(status);
    QTest::mouseClick(button, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QStringLiteral("successful")), 10000);
}

} // namespace KeyDis::Test
