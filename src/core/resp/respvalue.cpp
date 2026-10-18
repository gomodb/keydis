// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "core/resp/respvalue.h"

#include <QStringList>

namespace KeyDis {

namespace {

QString indentText(int indent)
{
    return QString(indent * 2, QLatin1Char(' '));
}

QString displayScalar(const QByteArray &bytes)
{
    if (bytes.isEmpty())
        return QStringLiteral("\"\"");
    const QString text = QString::fromUtf8(bytes);
    const bool binary = bytes.contains('\0')
        || text.contains(QChar::ReplacementCharacter)
        || std::any_of(text.cbegin(), text.cend(), [](QChar ch) {
               return ch.category() == QChar::Other_Control && ch != QLatin1Char('\n')
                   && ch != QLatin1Char('\r') && ch != QLatin1Char('\t');
           });
    if (binary)
        return QStringLiteral("0x") + QString::fromLatin1(bytes.toHex());
    QString escaped = text;
    escaped.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    escaped.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    escaped.replace(QLatin1Char('\t'), QStringLiteral("\\t"));
    return QStringLiteral("\"%1\"").arg(escaped);
}

} // namespace

bool RespValue::isError() const
{
    return type == RespType::Error || type == RespType::BulkError;
}

bool RespValue::isNull() const
{
    return type == RespType::Null;
}

QString RespValue::typeName() const
{
    switch (type) {
    case RespType::SimpleString:
        return QStringLiteral("simple-string");
    case RespType::Error:
        return QStringLiteral("error");
    case RespType::Integer:
        return QStringLiteral("integer");
    case RespType::BulkString:
        return QStringLiteral("bulk-string");
    case RespType::Array:
        return QStringLiteral("array");
    case RespType::Null:
        return QStringLiteral("null");
    case RespType::Boolean:
        return QStringLiteral("boolean");
    case RespType::Double:
        return QStringLiteral("double");
    case RespType::BigNumber:
        return QStringLiteral("big-number");
    case RespType::BulkError:
        return QStringLiteral("bulk-error");
    case RespType::Verbatim:
        return QStringLiteral("verbatim");
    case RespType::Map:
        return QStringLiteral("map");
    case RespType::Set:
        return QStringLiteral("set");
    case RespType::Attribute:
        return QStringLiteral("attribute");
    case RespType::Push:
        return QStringLiteral("push");
    }
    return QStringLiteral("unknown");
}

QString RespValue::toDisplayString(int indent) const
{
    const QString prefix = indentText(indent);
    switch (type) {
    case RespType::SimpleString:
        return prefix + scalar;
    case RespType::Error:
    case RespType::BulkError:
        return prefix + QStringLiteral("(error) ") + QString::fromUtf8(scalar);
    case RespType::Integer:
        return prefix + QString::number(integer);
    case RespType::BulkString:
        return prefix + displayScalar(scalar);
    case RespType::Null:
        return prefix + QStringLiteral("(nil)");
    case RespType::Boolean:
        return prefix + (integer ? QStringLiteral("true") : QStringLiteral("false"));
    case RespType::Double:
        return prefix + QString::number(real, 'g', 17);
    case RespType::BigNumber:
        return prefix + QStringLiteral("(big) ") + QString::fromLatin1(scalar);
    case RespType::Verbatim:
        return prefix + QStringLiteral("(%1) ").arg(verbatimType) + displayScalar(scalar);
    case RespType::Array:
    case RespType::Set:
    case RespType::Push: {
        QStringList lines;
        lines << prefix + QStringLiteral("[%1]").arg(typeName());
        for (const auto &element : elements)
            lines << element.toDisplayString(indent + 1);
        return lines.join(QLatin1Char('\n'));
    }
    case RespType::Map:
    case RespType::Attribute: {
        QStringList lines;
        lines << prefix + QStringLiteral("{%1}").arg(typeName());
        for (const auto &[key, value] : pairs) {
            lines << key.toDisplayString(indent + 1) + QStringLiteral(" => ")
                    + value.toDisplayString(indent + 1).trimmed();
        }
        return lines.join(QLatin1Char('\n'));
    }
    }
    return prefix + QStringLiteral("<unknown>");
}

} // namespace KeyDis
