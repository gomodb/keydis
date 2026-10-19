// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "test_mainwindow.h"
#include "test_profiledialog.h"
#include "test_valuepanel.h"

#include <QApplication>
#include <QTest>

#include <KLocalizedString>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("keydis"));
    KLocalizedString::setApplicationDomain("keydis");
    KeyDis::Test::TestMainWindow test;
    int status = QTest::qExec(&test, argc, argv);
    KeyDis::Test::TestValuePanel valuePanel;
    status |= QTest::qExec(&valuePanel, argc, argv);
    KeyDis::Test::TestProfileDialog profileDialog;
    status |= QTest::qExec(&profileDialog, argc, argv);
    return status;
}
