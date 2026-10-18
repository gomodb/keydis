// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "core/analysis/memoryanalyzer.h"

#include "core/net/redisclient.h"

#include <algorithm>
#include <utility>

namespace KeyDis {

struct MemoryAnalyzer::Page {
    QByteArray nextCursor;
    QVector<QByteArray> keys;
    QVector<QString> types;
    QVector<qint64> bytes;
    QVector<qint64> ttls;
    int remaining = 0;
};

MemoryAnalyzer::MemoryAnalyzer(RedisClient *connection, QObject *parent)
    : QObject(parent)
    , m_connection(connection)
{
}

MemoryAnalyzer::~MemoryAnalyzer() = default;

void MemoryAnalyzer::start(const QByteArray &pattern, const QString &type)
{
    if (m_running)
        return;
    ++m_generation;
    m_running = true;
    m_cancelled = false;
    m_pattern = pattern.isEmpty() ? QByteArrayLiteral("*") : pattern;
    m_typeFilter = type == QLatin1String("all") ? QString() : type;
    m_cursor = QByteArrayLiteral("0");
    m_result = MemoryAnalysisResult{};
    m_scanned = 0;
    emit progress(0);
    scanNext();
}

void MemoryAnalyzer::scanNext()
{
    if (!m_running || m_cancelled)
        return;
    if (!m_connection || !m_connection->isReady()) {
        m_running = false;
        emit finished(false, m_result, tr("Not connected"));
        return;
    }
    const quint64 generation = m_generation;
    QPointer<MemoryAnalyzer> guard(this);
    m_connection->sendCommand({QByteArrayLiteral("SCAN"), m_cursor, QByteArrayLiteral("MATCH"),
                               m_pattern, QByteArrayLiteral("COUNT"), QByteArrayLiteral("500")},
                              [guard, generation](const RespValue &reply, const QString &error) {
        if (!guard || generation != guard->m_generation)
            return;
        if (!error.isEmpty() || reply.isError() || reply.type != RespType::Array
            || reply.elements.size() != 2) {
            guard->m_running = false;
            emit guard->finished(false, guard->m_result,
                                 error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
            return;
        }
        const QByteArray nextCursor = reply.elements[0].scalar;
        QVector<QByteArray> keys;
        for (const RespValue &key : reply.elements[1].elements) {
            if (key.type == RespType::BulkString)
                keys.append(key.scalar);
        }
        if (keys.isEmpty()) {
            if (nextCursor == QByteArrayLiteral("0")) {
                guard->m_running = false;
                emit guard->finished(true, guard->m_result, {});
            } else {
                guard->m_cursor = nextCursor;
                guard->scanNext();
            }
            return;
        }
        guard->analyzePage(keys, nextCursor);
    });
}

void MemoryAnalyzer::analyzePage(const QVector<QByteArray> &keys, const QByteArray &nextCursor)
{
    if (!m_running || m_cancelled)
        return;
    auto page = std::make_shared<Page>();
    page->nextCursor = nextCursor;
    page->keys = keys;
    page->types.resize(keys.size());
    page->bytes.fill(-1, keys.size());
    page->ttls.fill(-1, keys.size());
    page->remaining = int(keys.size()) * 3;

    const quint64 generation = m_generation;
    QPointer<MemoryAnalyzer> guard(this);
    for (int index = 0; index < keys.size(); ++index) {
        const QByteArray key = keys.at(index);
        m_connection->sendCommand({QByteArrayLiteral("TYPE"), key},
                                  [guard, generation, page, index](const RespValue &reply,
                                                                   const QString &error) {
            if (!guard || generation != guard->m_generation)
                return;
            if (error.isEmpty() && !reply.isError())
                page->types[index] = QString::fromUtf8(reply.scalar);
            if (--page->remaining == 0)
                guard->processPage(page);
        });
        m_connection->sendCommand({QByteArrayLiteral("MEMORY"), QByteArrayLiteral("USAGE"), key},
                                  [guard, generation, page, index](const RespValue &reply,
                                                                   const QString &error) {
            if (!guard || generation != guard->m_generation)
                return;
            if (error.isEmpty() && reply.type == RespType::Integer)
                page->bytes[index] = reply.integer;
            if (--page->remaining == 0)
                guard->processPage(page);
        });
        m_connection->sendCommand({QByteArrayLiteral("PTTL"), key},
                                  [guard, generation, page, index](const RespValue &reply,
                                                                   const QString &error) {
            if (!guard || generation != guard->m_generation)
                return;
            if (error.isEmpty() && reply.type == RespType::Integer)
                page->ttls[index] = reply.integer;
            if (--page->remaining == 0)
                guard->processPage(page);
        });
    }
}

void MemoryAnalyzer::processPage(std::shared_ptr<Page> page)
{
    if (!m_running || m_cancelled)
        return;
    for (int index = 0; index < page->keys.size(); ++index) {
        if (page->bytes.at(index) < 0)
            continue;
        const QString type = page->types.at(index).isEmpty()
            ? QStringLiteral("unknown")
            : page->types.at(index);
        if (!m_typeFilter.isEmpty() && type != m_typeFilter)
            continue;
        KeyMemory memory;
        memory.key = page->keys.at(index);
        memory.type = type;
        memory.bytes = page->bytes.at(index);
        memory.ttlMs = page->ttls.at(index);
        m_result.keys.append(memory);
        m_result.totalBytes += memory.bytes;
        m_result.keyCount++;
        m_result.bytesByType[type] += memory.bytes;
        m_result.countByType[type]++;
        ++m_scanned;
    }
    emit progress(m_scanned);
    if (page->nextCursor == QByteArrayLiteral("0")) {
        std::sort(m_result.keys.begin(), m_result.keys.end(),
                  [](const KeyMemory &left, const KeyMemory &right) {
                      return left.bytes > right.bytes;
                  });
        m_running = false;
        emit finished(true, m_result, {});
    } else {
        m_cursor = page->nextCursor;
        scanNext();
    }
}

void MemoryAnalyzer::cancel()
{
    if (!m_running)
        return;
    m_cancelled = true;
    ++m_generation;
    m_running = false;
    emit finished(false, m_result, tr("Analysis cancelled"));
}

bool MemoryAnalyzer::isRunning() const
{
    return m_running;
}

} // namespace KeyDis
