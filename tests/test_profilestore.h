// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QObject>

namespace KeyDis::Test {

class TestProfileStore : public QObject
{
    Q_OBJECT

private slots:
    void roundTripsProfiles();
    void rejectsMalformedJson();
};

} // namespace KeyDis::Test
