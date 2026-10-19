// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QObject>

namespace KeyDis::Test {

class TestRedisIntegration : public QObject
{
    Q_OBJECT

private slots:
    void realServerRoundTrip();
    void pubSubRoundTrip();
    void exportImportRoundTrip();
    void memoryAnalysisRoundTrip();
    void sentinelResolvesMaster();
    void sentinelWatcherReceivesFailover();
    void transactionRoundTrip();
    void unixSocketRoundTrip();
};

} // namespace KeyDis::Test
