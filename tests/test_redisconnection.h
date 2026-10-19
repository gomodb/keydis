// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QObject>

namespace KeyDis::Test {

class TestRedisConnection : public QObject
{
    Q_OBJECT

private slots:
    void handshakeSelectAndPing();
    void fallsBackToResp2();
    void rejectsMissingTlsFiles();
};

} // namespace KeyDis::Test
