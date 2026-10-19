// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QObject>

namespace KeyDis::Test {

class TestMsgPack : public QObject
{
    Q_OBJECT

private slots:
    void decodesObjects();
    void rejectsInvalidData();
};

} // namespace KeyDis::Test
