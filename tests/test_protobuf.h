// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QObject>

namespace KeyDis::Test {

class TestProtobuf : public QObject
{
    Q_OBJECT

private slots:
    void decodesWireFields();
    void rejectsInvalidData();
};

} // namespace KeyDis::Test
