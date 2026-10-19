// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "core/format/protobufdecoder.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace KeyDis {

namespace {

constexpr int kMaxDepth = 64;
constexpr int kMaxOutput = 8 * 1024 * 1024;

bool readVarint(const QByteArray &data, int *offset, quint64 *value)
{
    quint64 result = 0;
    int shift = 0;
    while (*offset < data.size() && shift <= 63) {
        const quint64 byte = static_cast<unsigned char>(data.at((*offset)++));
        result |= (byte & 0x7fU) << shift;
        if ((byte & 0x80U) == 0) {
            *value = result;
            return true;
        }
        shift += 7;
    }
    return false;
}

QString quotedString(const QByteArray &bytes)
{
    QJsonArray array;
    array.append(QString::fromUtf8(bytes));
    const QByteArray encoded = QJsonDocument(array).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(encoded.mid(1, encoded.size() - 2));
}

bool printableString(const QByteArray &bytes)
{
    if (bytes.isEmpty())
        return false;
    const QString text = QString::fromUtf8(bytes);
    if (text.toUtf8() != bytes)
        return false;
    for (const QChar character : text) {
        if (character.unicode() < 0x20 && character != QLatin1Char('\n')
            && character != QLatin1Char('\t') && character != QLatin1Char('\r')) {
            return false;
        }
    }
    return true;
}

QString hexPreview(const QByteArray &bytes, int limit = 16)
{
    QString text;
    for (int index = 0; index < bytes.size() && index < limit; ++index) {
        if (index > 0)
            text += QLatin1Char(' ');
        text += QStringLiteral("%1")
                    .arg(static_cast<unsigned char>(bytes.at(index)), 2, 16, QLatin1Char('0'))
                    .toUpper();
    }
    if (bytes.size() > limit)
        text += QStringLiteral(" …");
    return text;
}

bool parseMessage(const QByteArray &data, int depth, int *budget, QString *output, QString *error);

bool parseLengthDelimited(const QByteArray &bytes, int depth, int *budget, QString *value,
                          QString *error)
{
    if (printableString(bytes)) {
        *value = quotedString(bytes);
        return true;
    }
    QString nested;
    if (parseMessage(bytes, depth + 1, budget, &nested, nullptr) && !nested.isEmpty()) {
        *value = QStringLiteral("{ ") + nested + QStringLiteral(" }");
        return true;
    }
    if (error)
        *error = QStringLiteral("Invalid Protobuf length-delimited field");
    *value = QStringLiteral("<%1 bytes: %2>").arg(bytes.size()).arg(hexPreview(bytes));
    return true;
}

bool parseMessage(const QByteArray &data, int depth, int *budget, QString *output, QString *error)
{
    if (depth > kMaxDepth) {
        if (error)
            *error = QStringLiteral("Protobuf nesting is too deep");
        return false;
    }
    int offset = 0;
    QStringList lines;
    while (offset < data.size()) {
        if (--(*budget) <= 0) {
            if (error)
                *error = QStringLiteral("Protobuf output exceeds the 8 MiB limit");
            return false;
        }
        quint64 key = 0;
        if (!readVarint(data, &offset, &key) || key == 0) {
            if (error)
                *error = QStringLiteral("Invalid Protobuf field key");
            return false;
        }
        const quint64 field = key >> 3U;
        const int wire = static_cast<int>(key & 0x7U);
        const QString indent(depth * 2, QLatin1Char(' '));
        QString value;
        switch (wire) {
        case 0: {
            quint64 number = 0;
            if (!readVarint(data, &offset, &number)) {
                if (error)
                    *error = QStringLiteral("Truncated Protobuf varint");
                return false;
            }
            value = QStringLiteral("%1 (varint): %2").arg(field).arg(number);
            break;
        }
        case 1: {
            if (offset + 8 > data.size()) {
                if (error)
                    *error = QStringLiteral("Truncated Protobuf fixed64");
                return false;
            }
            value = QStringLiteral("%1 (fixed64): %2")
                        .arg(field)
                        .arg(hexPreview(data.mid(offset, 8), 8));
            offset += 8;
            break;
        }
        case 2: {
            quint64 length = 0;
            if (!readVarint(data, &offset, &length)
                || length > static_cast<quint64>(data.size() - offset)) {
                if (error)
                    *error = QStringLiteral("Truncated Protobuf length-delimited field");
                return false;
            }
            const QByteArray bytes = data.mid(offset, static_cast<int>(length));
            offset += static_cast<int>(length);
            QString inner;
            if (!parseLengthDelimited(bytes, depth, budget, &inner, error))
                return false;
            value = QStringLiteral("%1 (length-delimited): %2").arg(field).arg(inner);
            break;
        }
        case 5: {
            if (offset + 4 > data.size()) {
                if (error)
                    *error = QStringLiteral("Truncated Protobuf fixed32");
                return false;
            }
            value = QStringLiteral("%1 (fixed32): %2")
                        .arg(field)
                        .arg(hexPreview(data.mid(offset, 4), 4));
            offset += 4;
            break;
        }
        case 3:
        case 4:
            value = QStringLiteral("%1 (group, unsupported)").arg(field);
            break;
        default:
            if (error)
                *error = QStringLiteral("Unknown Protobuf wire type %1").arg(wire);
            return false;
        }
        lines.append(indent + value);
    }
    *output = lines.join(QStringLiteral("\n"));
    return true;
}

} // namespace

QString ProtobufDecoder::decode(const QByteArray &data, QString *error)
{
    if (data.isEmpty()) {
        if (error)
            *error = QStringLiteral("Empty Protobuf input");
        return {};
    }
    int budget = kMaxOutput;
    QString output;
    if (!parseMessage(data, 0, &budget, &output, error))
        return {};
    return output;
}

} // namespace KeyDis
