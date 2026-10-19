// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QObject>

namespace KeyDis::Test {

class TestValuePanel : public QObject
{
    Q_OBJECT

private slots:
    void buildsAndEditsJsonTree();
    void buildsProtobufView();
    void buildsSpecialTypeViews();
    void buildsStreamConsumerView();
    void loadsLargeStringInChunks();
};

} // namespace KeyDis::Test
