// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QObject>

namespace KeyDis::Test {

class TestCommandEncoder : public QObject
{
    Q_OBJECT

private slots:
    void encodesArguments();
    void parsesQuotedArguments();
    void rejectsUnterminatedQuote();
};

} // namespace KeyDis::Test
