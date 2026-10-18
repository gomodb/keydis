// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include "core/conn/connectionprofile.h"

#include <QString>
#include <QVector>

namespace KeyDis {

class ProfileStore
{
public:
    explicit ProfileStore(QString filePath = {});

    bool load(QVector<ConnectionProfile> *profiles, QString *error = nullptr) const;
    bool save(const QVector<ConnectionProfile> &profiles, QString *error = nullptr) const;

    [[nodiscard]] QString filePath() const;

private:
    QString m_filePath;
};

} // namespace KeyDis
