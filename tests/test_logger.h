// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QObject>

namespace KeyDis::Test {

class TestLogger : public QObject
{
    Q_OBJECT

private slots:
    void redactsCredentials();
    void rotatesLogFiles();
};

} // namespace KeyDis::Test
