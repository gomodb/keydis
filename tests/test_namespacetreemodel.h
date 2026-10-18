// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QObject>

namespace KeyDis::Test {

class TestNamespaceTreeModel : public QObject
{
    Q_OBJECT

private slots:
    void buildsNamespacesAndCounts();
    void removesKeysAndUpdatesCounts();
};

} // namespace KeyDis::Test
