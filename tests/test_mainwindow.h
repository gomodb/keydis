// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QObject>

namespace KeyDis::Test {

class TestMainWindow : public QObject
{
    Q_OBJECT

private slots:
    void browsesAndShowsValues();
};

} // namespace KeyDis::Test
