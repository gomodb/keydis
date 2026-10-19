// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QObject>

namespace KeyDis::Test {

class TestByteFormat : public QObject
{
    Q_OBJECT

private slots:
    void roundTripsHexAndBase64();
    void rejectsInvalidInput();
};

} // namespace KeyDis::Test
