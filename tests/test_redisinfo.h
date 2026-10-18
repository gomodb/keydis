// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QObject>

namespace KeyDis::Test {

class TestRedisInfo : public QObject
{
    Q_OBJECT

private slots:
    void parsesFieldsAndKeyspace();
};

} // namespace KeyDis::Test
