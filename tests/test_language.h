// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QObject>

namespace KeyDis::Test {

class TestLanguage : public QObject
{
    Q_OBJECT

private slots:
    void normalizesPreferences();
    void resolvesSystemLanguage();
    void explicitPreferenceWins();
    void loadsChineseCatalog();
};

} // namespace KeyDis::Test
