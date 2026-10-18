// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "test_logger.h"

#include "core/app_paths.h"
#include "core/logger.h"

#include <QFile>
#include <QTest>

namespace KeyDis::Test {

void TestLogger::redactsCredentials()
{
    QCOMPARE(Logger::redact(QStringLiteral("AUTH default hunter2")),
             QStringLiteral("AUTH ***"));
    QCOMPARE(Logger::redact(QStringLiteral("password=hunter2")),
             QStringLiteral("password=***"));
    QCOMPARE(Logger::redact(QStringLiteral("ordinary log line")),
             QStringLiteral("ordinary log line"));
}

void TestLogger::rotatesLogFiles()
{
    Logger::install();
    Logger::setLevel(QStringLiteral("debug"));
    Logger::setRetention(1, 2);

    const QString path = AppPaths::logFile();
    QFile::remove(path);
    QFile::remove(path + QStringLiteral(".1"));
    QFile::remove(path + QStringLiteral(".2"));

    const QString payload(600 * 1024, QLatin1Char('x'));
    Logger::log(QStringLiteral("test"), payload);
    Logger::log(QStringLiteral("test"), payload);
    Logger::log(QStringLiteral("test"), payload);

    QVERIFY(QFile::exists(path));
    QVERIFY(QFile::exists(path + QStringLiteral(".1")));
}

} // namespace KeyDis::Test
