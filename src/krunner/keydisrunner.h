// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <KRunner/AbstractRunner>

namespace KeyDis {

class KeyDisRunner : public KRunner::AbstractRunner
{
    Q_OBJECT

public:
    KeyDisRunner(QObject *parent, const KPluginMetaData &metaData);

    void match(KRunner::RunnerContext &context) override;
    void run(const KRunner::RunnerContext &context, const KRunner::QueryMatch &match) override;
};

} // namespace KeyDis
