// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QString>

#include <utility>
#include <vector>

namespace KeyDis {

enum class RespType {
    SimpleString,
    Error,
    Integer,
    BulkString,
    Array,
    Null,
    Boolean,
    Double,
    BigNumber,
    BulkError,
    Verbatim,
    Map,
    Set,
    Attribute,
    Push
};

struct RespValue {
    RespType type = RespType::Null;
    QByteArray scalar;
    qint64 integer = 0;
    double real = 0.0;
    QString verbatimType;
    std::vector<RespValue> elements;
    std::vector<std::pair<RespValue, RespValue>> pairs;

    [[nodiscard]] bool isError() const;
    [[nodiscard]] bool isNull() const;
    [[nodiscard]] QString typeName() const;
    [[nodiscard]] QString toDisplayString(int indent = 0) const;
};

} // namespace KeyDis

Q_DECLARE_METATYPE(KeyDis::RespValue)
