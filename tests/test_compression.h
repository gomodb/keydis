// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QObject>

namespace KeyDis::Test {

class TestCompression : public QObject
{
    Q_OBJECT

private slots:
    void detectsAndDecompresses();
    void rejectsInvalidData();
};

} // namespace KeyDis::Test
