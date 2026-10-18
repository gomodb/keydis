// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QObject>

namespace KeyDis::Test {

class TestKeyScanner : public QObject
{
    Q_OBJECT

private slots:
    void scansAndLoadsMore();
    void scansAllPages();
    void fallsBackWhenTypeFilterUnsupported();
};

} // namespace KeyDis::Test
