// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QObject>

namespace KeyDis::Test {

class TestSshTunnel : public QObject
{
    Q_OBJECT

private slots:
    void failsForUnreachableHost();
    void dynamicTunnelFailsForUnreachableHost();
};

} // namespace KeyDis::Test
