// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include "core/resp/respvalue.h"

#include <QByteArray>
#include <QString>

#include <memory>

struct redisReader;

namespace KeyDis {

class HiredisReader
{
public:
    enum class ReadResult {
        Reply,
        NeedMoreData,
        Error
    };

    explicit HiredisReader(qsizetype maxReplyBytes = 64 * 1024 * 1024,
                           qsizetype maxElements = 1'000'000, int maxDepth = 128);
    ~HiredisReader();

    HiredisReader(const HiredisReader &) = delete;
    HiredisReader &operator=(const HiredisReader &) = delete;

    bool feed(const char *data, qsizetype size, QString *error);
    ReadResult read(RespValue *value, QString *error);
    void reset();

    [[nodiscard]] qsizetype bytesSinceReply() const;
    [[nodiscard]] qsizetype maxReplyBytes() const;

private:
    struct ReaderDeleter {
        void operator()(redisReader *reader) const;
    };

    std::unique_ptr<redisReader, ReaderDeleter> m_reader;
    qsizetype m_maxReplyBytes;
    qsizetype m_maxElements;
    int m_maxDepth;
    qsizetype m_bytesSinceReply = 0;
};

} // namespace KeyDis
