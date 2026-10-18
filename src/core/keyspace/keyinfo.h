// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QByteArray>
#include <QString>

namespace KeyDis {

struct KeyInfo {
    QByteArray key;
    QString type = QStringLiteral("unknown");
    qint64 ttlMs = -2;

    [[nodiscard]] bool isValid() const
    {
        return !key.isEmpty() && type != QLatin1String("none");
    }

    [[nodiscard]] QString ttlText() const;
};

QString formatTtl(qint64 ttlMs);

} // namespace KeyDis
