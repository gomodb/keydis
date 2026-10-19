// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "test_language.h"

#include "core/language.h"

#include <QCoreApplication>
#include <QLocale>
#include <QTranslator>
#include <QTest>

namespace KeyDis::Test {

void TestLanguage::normalizesPreferences()
{
    QCOMPARE(Language::normalizePreference(QStringLiteral("en")), QStringLiteral("en"));
    QCOMPARE(Language::normalizePreference(QStringLiteral("en_US")), QStringLiteral("en"));
    QCOMPARE(Language::normalizePreference(QStringLiteral("zh")), QStringLiteral("zh_CN"));
    QCOMPARE(Language::normalizePreference(QStringLiteral("zh_CN")), QStringLiteral("zh_CN"));
    QCOMPARE(Language::normalizePreference(QStringLiteral("system")), QStringLiteral("system"));
    QCOMPARE(Language::normalizePreference(QStringLiteral("fr")), QStringLiteral("system"));
}

void TestLanguage::resolvesSystemLanguage()
{
    QCOMPARE(Language::resolve(QStringLiteral("system"), QLocale::Chinese),
             QStringLiteral("zh_CN"));
    QCOMPARE(Language::resolve(QStringLiteral("system"), QLocale::English), QStringLiteral("en"));
    QCOMPARE(Language::resolve(QStringLiteral("system"), QLocale::French), QStringLiteral("en"));
    QCOMPARE(Language::resolve(QString(), QLocale::French), QStringLiteral("en"));
}

void TestLanguage::explicitPreferenceWins()
{
    QCOMPARE(Language::resolve(QStringLiteral("en"), QLocale::Chinese), QStringLiteral("en"));
    QCOMPARE(Language::resolve(QStringLiteral("zh_CN"), QLocale::English),
             QStringLiteral("zh_CN"));
}

void TestLanguage::loadsChineseCatalog()
{
    QTranslator applicationTranslator;
    QTranslator qtTranslator;
    auto *application = QCoreApplication::instance();
    QVERIFY(application != nullptr);
    QVERIFY(Language::installTranslator(*application, QStringLiteral("zh_CN"),
                                        &applicationTranslator, &qtTranslator));
    QCOMPARE(QCoreApplication::translate("KeyDis::DashboardPage", "Overview"),
             QString::fromUtf8("概览"));
    QCOMPARE(QCoreApplication::translate("KeyDis::MainWindow", "Scan"),
             QString::fromUtf8("扫描"));
}

} // namespace KeyDis::Test
