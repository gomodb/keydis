// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "core/resp/hiredisreader.h"

#if __has_include(<hiredis/hiredis.h>)
#include <hiredis/hiredis.h>
#else
#include <hiredis.h>
#endif

#include <utility>

namespace KeyDis {

namespace {

QString readerError(const redisReader *reader)
{
    if (!reader)
        return QStringLiteral("hiredis reader is not available");
    return QString::fromLatin1(redisReaderGetError(const_cast<redisReader *>(reader)));
}

void configureReader(redisReader *reader, qsizetype maxElements, int maxDepth)
{
    if (!reader)
        return;
    reader->maxelements = static_cast<long long>(maxElements);
#if defined(HIREDIS_MAJOR) && (HIREDIS_MAJOR > 1 || (HIREDIS_MAJOR == 1 && HIREDIS_MINOR >= 5))
    reader->maxdepth = maxDepth;
#else
    Q_UNUSED(maxDepth)
#endif
}

bool convertReply(const redisReply *reply, RespValue *out, int depth, int maxDepth,
                  qsizetype maxElements, QString *error);

bool convertElements(const redisReply *reply, std::vector<RespValue> *out, int depth, int maxDepth,
                     qsizetype maxElements, QString *error)
{
    if (reply->elements > static_cast<size_t>(maxElements)) {
        if (error)
            *error = QStringLiteral("RESP aggregate contains too many elements (%1)")
                         .arg(reply->elements);
        return false;
    }
    out->reserve(reply->elements);
    for (size_t index = 0; index < reply->elements; ++index) {
        RespValue element;
        if (!convertReply(reply->element[index], &element, depth + 1, maxDepth, maxElements,
                          error)) {
            return false;
        }
        out->push_back(std::move(element));
    }
    return true;
}

bool convertReply(const redisReply *reply, RespValue *out, int depth, int maxDepth,
                  qsizetype maxElements, QString *error)
{
    if (!reply || !out) {
        if (error)
            *error = QStringLiteral("hiredis returned a null reply");
        return false;
    }
    if (depth > maxDepth) {
        if (error)
            *error = QStringLiteral("RESP reply nesting exceeds %1 levels").arg(maxDepth);
        return false;
    }

    *out = RespValue{};
    switch (reply->type) {
    case REDIS_REPLY_STATUS:
        out->type = RespType::SimpleString;
        out->scalar = QByteArray(reply->str, static_cast<qsizetype>(reply->len));
        return true;
    case REDIS_REPLY_ERROR:
        out->type = RespType::Error;
        out->scalar = QByteArray(reply->str, static_cast<qsizetype>(reply->len));
        return true;
    case REDIS_REPLY_INTEGER:
        out->type = RespType::Integer;
        out->integer = reply->integer;
        return true;
    case REDIS_REPLY_NIL:
        out->type = RespType::Null;
        return true;
    case REDIS_REPLY_STRING:
        out->type = RespType::BulkString;
        out->scalar = QByteArray(reply->str, static_cast<qsizetype>(reply->len));
        return true;
    case REDIS_REPLY_ARRAY:
        out->type = RespType::Array;
        return convertElements(reply, &out->elements, depth, maxDepth, maxElements, error);
    case REDIS_REPLY_SET:
        out->type = RespType::Set;
        return convertElements(reply, &out->elements, depth, maxDepth, maxElements, error);
    case REDIS_REPLY_PUSH:
        out->type = RespType::Push;
        return convertElements(reply, &out->elements, depth, maxDepth, maxElements, error);
    case REDIS_REPLY_MAP:
    case REDIS_REPLY_ATTR: {
        out->type = reply->type == REDIS_REPLY_MAP ? RespType::Map : RespType::Attribute;
        if (reply->elements % 2 != 0) {
            if (error)
                *error = QStringLiteral("RESP map/attribute has an odd number of elements");
            return false;
        }
        const size_t pairCount = reply->elements / 2;
        if (pairCount > static_cast<size_t>(maxElements)) {
            if (error)
                *error = QStringLiteral("RESP map contains too many pairs (%1)").arg(pairCount);
            return false;
        }
        out->mapKeys.reserve(pairCount);
        out->mapValues.reserve(pairCount);
        for (size_t index = 0; index < pairCount; ++index) {
            RespValue key;
            RespValue value;
            if (!convertReply(reply->element[index * 2], &key, depth + 1, maxDepth, maxElements,
                              error)
                || !convertReply(reply->element[index * 2 + 1], &value, depth + 1, maxDepth,
                                 maxElements, error)) {
                return false;
            }
            out->mapKeys.push_back(key.scalar);
            out->mapValues.push_back(std::move(value));
        }
        return true;
    }
    case REDIS_REPLY_BOOL:
        out->type = RespType::Boolean;
        out->integer = reply->integer;
        return true;
    case REDIS_REPLY_DOUBLE:
        out->type = RespType::Double;
        out->real = reply->dval;
        if (reply->str)
            out->scalar = QByteArray(reply->str, static_cast<qsizetype>(reply->len));
        return true;
    case REDIS_REPLY_BIGNUM:
        out->type = RespType::BigNumber;
        out->scalar = QByteArray(reply->str, static_cast<qsizetype>(reply->len));
        return true;
    case REDIS_REPLY_VERB:
        out->type = RespType::Verbatim;
        out->scalar = QByteArray(reply->str, static_cast<qsizetype>(reply->len));
        out->verbatimType = QString::fromLatin1(reply->vtype, 3);
        return true;
    default:
        if (error)
            *error = QStringLiteral("unsupported RESP reply type %1").arg(reply->type);
        return false;
    }
}

} // namespace

void HiredisReader::ReaderDeleter::operator()(redisReader *reader) const
{
    if (reader)
        redisReaderFree(reader);
}

HiredisReader::HiredisReader(qsizetype maxReplyBytes, qsizetype maxElements, int maxDepth)
    : m_reader(redisReaderCreate())
    , m_maxReplyBytes(maxReplyBytes)
    , m_maxElements(maxElements)
    , m_maxDepth(maxDepth)
{
    configureReader(m_reader.get(), m_maxElements, m_maxDepth);
}

HiredisReader::~HiredisReader() = default;

void HiredisReader::reset()
{
    m_reader.reset(redisReaderCreate());
    m_bytesSinceReply = 0;
    configureReader(m_reader.get(), m_maxElements, m_maxDepth);
}

bool HiredisReader::feed(const char *data, qsizetype size, QString *error)
{
    if (!m_reader) {
        if (error)
            *error = QStringLiteral("hiredis reader is not available");
        return false;
    }
    if (size < 0 || (size > 0 && !data)) {
        if (error)
            *error = QStringLiteral("invalid RESP feed buffer");
        return false;
    }
    if (m_bytesSinceReply + size > m_maxReplyBytes) {
        if (error)
            *error = QStringLiteral("RESP reply exceeds the %1 byte budget").arg(m_maxReplyBytes);
        return false;
    }

    m_bytesSinceReply += size;
    if (redisReaderFeed(m_reader.get(), data, static_cast<size_t>(size)) != REDIS_OK) {
        if (error)
            *error = readerError(m_reader.get());
        return false;
    }
    return true;
}

HiredisReader::ReadResult HiredisReader::read(RespValue *value, QString *error)
{
    if (!m_reader || !value) {
        if (error)
            *error = QStringLiteral("hiredis reader is not available");
        return ReadResult::Error;
    }

    void *raw = nullptr;
    const int result = redisReaderGetReply(m_reader.get(), &raw);
    if (result != REDIS_OK) {
        if (error)
            *error = readerError(m_reader.get());
        return ReadResult::Error;
    }
    if (!raw)
        return ReadResult::NeedMoreData;

    auto *reply = static_cast<redisReply *>(raw);
    const bool ok = convertReply(reply, value, 0, m_maxDepth, m_maxElements, error);
    freeReplyObject(reply);
    if (!ok)
        return ReadResult::Error;

    m_bytesSinceReply = 0;
    return ReadResult::Reply;
}

qsizetype HiredisReader::bytesSinceReply() const
{
    return m_bytesSinceReply;
}

qsizetype HiredisReader::maxReplyBytes() const
{
    return m_maxReplyBytes;
}

} // namespace KeyDis
