// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "core/format/msgpackdecoder.h"

#include <msgpack.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

namespace KeyDis {

namespace {

constexpr int kMaxDepth = 64;
constexpr int kMaxOutput = 8 * 1024 * 1024;

QString quotedBytes(const QByteArray &bytes)
{
    const QString text = QString::fromUtf8(bytes);
    if (text.toUtf8() == bytes) {
        QJsonArray array;
        array.append(text);
        const QByteArray encoded = QJsonDocument(array).toJson(QJsonDocument::Compact);
        return QString::fromUtf8(encoded.mid(1, encoded.size() - 2));
    }
    return QStringLiteral("\"<binary %1 bytes: %2>\"")
        .arg(bytes.size())
        .arg(QString::fromLatin1(bytes.toBase64()));
}

QString formatObject(const msgpack_object &object, int depth, int *budget, QString *error)
{
    if (--(*budget) <= 0) {
        if (error)
            *error = QStringLiteral("MessagePack output exceeds the 8 MiB limit");
        return {};
    }
    if (depth > kMaxDepth) {
        if (error)
            *error = QStringLiteral("MessagePack nesting is too deep");
        return {};
    }
    const QString indent(depth * 2, QLatin1Char(' '));
    const QString childIndent((depth + 1) * 2, QLatin1Char(' '));
    switch (object.type) {
    case MSGPACK_OBJECT_NIL:
        return QStringLiteral("null");
    case MSGPACK_OBJECT_BOOLEAN:
        return object.via.boolean ? QStringLiteral("true") : QStringLiteral("false");
    case MSGPACK_OBJECT_POSITIVE_INTEGER:
        return QString::number(object.via.u64);
    case MSGPACK_OBJECT_NEGATIVE_INTEGER:
        return QString::number(object.via.i64);
    case MSGPACK_OBJECT_FLOAT32:
    case MSGPACK_OBJECT_FLOAT64:
        return QString::number(object.via.f64, 'g', 15);
    case MSGPACK_OBJECT_STR:
        return quotedBytes(QByteArray(object.via.str.ptr, static_cast<int>(object.via.str.size)));
    case MSGPACK_OBJECT_BIN:
        return QStringLiteral("\"<binary %1 bytes: %2>\"")
            .arg(object.via.bin.size)
            .arg(QString::fromLatin1(QByteArray(object.via.bin.ptr,
                                                static_cast<int>(object.via.bin.size))
                                         .toBase64()));
    case MSGPACK_OBJECT_EXT:
        return QStringLiteral("{\"$extType\": %1, \"$data\": \"%2\"}")
            .arg(object.via.ext.type)
            .arg(QString::fromLatin1(QByteArray(object.via.ext.ptr,
                                                static_cast<int>(object.via.ext.size))
                                         .toBase64()));
    case MSGPACK_OBJECT_ARRAY: {
        QStringList values;
        for (uint32_t index = 0; index < object.via.array.size; ++index) {
            const QString value = formatObject(object.via.array.ptr[index], depth + 1, budget,
                                               error);
            if (!error->isEmpty())
                return {};
            values.append(childIndent + value);
        }
        if (values.isEmpty())
            return QStringLiteral("[]");
        return QStringLiteral("[\n") + values.join(QStringLiteral(",\n"))
            + QLatin1Char('\n') + indent + QLatin1Char(']');
    }
    case MSGPACK_OBJECT_MAP: {
        QStringList values;
        for (uint32_t index = 0; index < object.via.map.size; ++index) {
            const msgpack_object &key = object.via.map.ptr[index].key;
            const msgpack_object &value = object.via.map.ptr[index].val;
            const QString keyText = key.type == MSGPACK_OBJECT_STR
                ? quotedBytes(QByteArray(key.via.str.ptr, static_cast<int>(key.via.str.size)))
                : quotedBytes(formatObject(key, depth + 1, budget, error).toUtf8());
            if (!error->isEmpty())
                return {};
            const QString valueText = formatObject(value, depth + 1, budget, error);
            if (!error->isEmpty())
                return {};
            values.append(childIndent + keyText + QStringLiteral(": ") + valueText);
        }
        if (values.isEmpty())
            return QStringLiteral("{}");
        return QStringLiteral("{\n") + values.join(QStringLiteral(",\n"))
            + QLatin1Char('\n') + indent + QLatin1Char('}');
    }
    }
    if (error)
        *error = QStringLiteral("Unsupported MessagePack object");
    return {};
}

} // namespace

QString MsgPackDecoder::decode(const QByteArray &data, QString *error)
{
    if (data.isEmpty()) {
        if (error)
            *error = QStringLiteral("Empty MessagePack input");
        return {};
    }
    msgpack_zone zone;
    if (!msgpack_zone_init(&zone, 2048)) {
        if (error)
            *error = QStringLiteral("Could not allocate MessagePack zone");
        return {};
    }
    msgpack_object object{};
    size_t offset = 0;
    const msgpack_unpack_return result =
        msgpack_unpack(data.constData(), static_cast<size_t>(data.size()), &offset, &zone, &object);
    if (result != MSGPACK_UNPACK_SUCCESS || offset != static_cast<size_t>(data.size())) {
        if (error)
            *error = QStringLiteral("Invalid or trailing MessagePack data");
        msgpack_zone_destroy(&zone);
        return {};
    }
    int budget = kMaxOutput;
    QString text = formatObject(object, 0, &budget, error);
    msgpack_zone_destroy(&zone);
    if (error && !error->isEmpty())
        return {};
    return text;
}

} // namespace KeyDis
