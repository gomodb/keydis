// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QObject>

namespace KeyDis::Test {

class TestResp : public QObject
{
    Q_OBJECT

private slots:
    void parsesSimpleTypes();
    void parsesAggregates();
    void parsesResp3Types();
    void handlesFragmentedInput();
    void rejectsOversizedReply();
    void rejectsTooManyElements();
    void rejectsTooDeepNesting();
};

} // namespace KeyDis::Test
