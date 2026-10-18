// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QObject>

namespace KeyDis::Test {

class TestClusterClient : public QObject
{
    Q_OBJECT

private slots:
    void routesKeysAndScans();
    void failsGracefully();
};

} // namespace KeyDis::Test
