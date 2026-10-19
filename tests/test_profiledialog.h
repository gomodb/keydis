// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QObject>

namespace KeyDis::Test {

class TestProfileDialog : public QObject
{
    Q_OBJECT

private slots:
    void testsDirectConnection();
};

} // namespace KeyDis::Test
