// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "test_mainwindow.h"

#include <QApplication>
#include <QTest>

#include <KLocalizedString>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("keydis"));
    KLocalizedString::setApplicationDomain("keydis");
    KeyDis::Test::TestMainWindow test;
    return QTest::qExec(&test, argc, argv);
}
