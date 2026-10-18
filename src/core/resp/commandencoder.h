// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

namespace KeyDis {

class CommandEncoder
{
public:
    static QByteArray encode(const QList<QByteArray> &args);
    static bool parseLine(const QString &line, QList<QByteArray> *args, QString *error);
};

} // namespace KeyDis
