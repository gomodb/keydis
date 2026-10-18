// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "core/keyspace/keyscanner.h"

#include "core/net/redisconnection.h"

#include <QTimer>

#include <utility>

namespace KeyDis {

KeyScanner::KeyScanner(RedisConnection *connection, QObject *parent)
    : QObject(parent)
    , m_connection(connection)
{
}

void KeyScanner::start(const QByteArray &pattern, const QString &type)
{
    ++m_generation;
    m_pattern = pattern.isEmpty() ? QByteArrayLiteral("*") : pattern;
    m_type = type == QLatin1String("all") ? QString() : type;
    m_cursor = QByteArrayLiteral("0");
    m_nextCursor = m_cursor;
    m_useType = !m_type.isEmpty();
    m_triedWithoutType = false;
    m_running = false;
    m_finished = false;
    m_autoContinue = false;
    m_scanned = 0;
    m_batch.clear();
    m_batchIndex.clear();
    m_metadataRemaining = 0;

    if (!m_connection || !m_connection->isReady()) {
        fail(QStringLiteral("Not connected"));
        return;
    }

    m_running = true;
    sendScan();
}

void KeyScanner::startAll(const QByteArray &pattern, const QString &type)
{
    start(pattern, type);
    m_autoContinue = !m_finished;
    if (m_autoContinue && !m_running)
        loadMore();
}

void KeyScanner::loadMore()
{
    if (m_running || m_finished)
        return;
    if (!m_connection || !m_connection->isReady()) {
        fail(QStringLiteral("Not connected"));
        return;
    }
    m_running = true;
    sendScan();
}

void KeyScanner::stop()
{
    ++m_generation;
    m_running = false;
    m_finished = true;
    m_autoContinue = false;
    m_batch.clear();
    m_batchIndex.clear();
    m_metadataRemaining = 0;
}

bool KeyScanner::isRunning() const
{
    return m_running;
}

bool KeyScanner::isFinished() const
{
    return m_finished;
}

qint64 KeyScanner::scanned() const
{
    return m_scanned;
}

void KeyScanner::sendScan()
{
    if (!m_connection || !m_connection->isReady()) {
        fail(QStringLiteral("Not connected"));
        return;
    }

    QList<QByteArray> args{QByteArrayLiteral("SCAN"), m_cursor};
    if (m_pattern != QByteArrayLiteral("*")) {
        args.append(QByteArrayLiteral("MATCH"));
        args.append(m_pattern);
    }
    args.append(QByteArrayLiteral("COUNT"));
    args.append(QByteArray::number(m_count));
    if (m_useType && !m_type.isEmpty()) {
        args.append(QByteArrayLiteral("TYPE"));
        args.append(m_type.toUtf8());
    }

    const quint64 generation = m_generation;
    QPointer<KeyScanner> guard(this);
    m_connection->sendCommand(args, [guard, generation](const RespValue &reply,
                                                       const QString &error) {
        if (!guard || guard->m_generation != generation)
            return;
        guard->handleScan(reply, error);
    });
}

void KeyScanner::handleScan(const RespValue &reply, const QString &error)
{
    if (!error.isEmpty()) {
        fail(error);
        return;
    }

    if (reply.isError()) {
        if (m_useType && !m_triedWithoutType) {
            m_useType = false;
            m_triedWithoutType = true;
            sendScan();
            return;
        }
        fail(QString::fromUtf8(reply.scalar));
        return;
    }

    if (reply.type != RespType::Array || reply.elements.size() != 2
        || reply.elements[1].type != RespType::Array) {
        fail(QStringLiteral("Unexpected SCAN reply shape"));
        return;
    }

    const RespValue &cursorValue = reply.elements[0];
    if (cursorValue.type != RespType::BulkString
        && cursorValue.type != RespType::SimpleString) {
        fail(QStringLiteral("Unexpected SCAN cursor type"));
        return;
    }
    m_nextCursor = cursorValue.scalar;

    const RespValue &keyArray = reply.elements[1];
    m_batch.clear();
    m_batchIndex.clear();
    m_batch.reserve(keyArray.elements.size());
    for (const RespValue &keyValue : keyArray.elements) {
        if (keyValue.type != RespType::BulkString)
            continue;
        KeyInfo info;
        info.key = keyValue.scalar;
        info.type = QStringLiteral("unknown");
        info.ttlMs = -2;
        const int index = m_batch.size();
        m_batch.append(info);
        m_batchIndex.insert(info.key, index);
    }

    if (m_batch.isEmpty()) {
        finishBatch();
        return;
    }

    m_metadataRemaining = m_batch.size() * 2;
    for (int index = 0; index < m_batch.size(); ++index)
        requestMetadata(index);
}

void KeyScanner::requestMetadata(int index)
{
    if (index < 0 || index >= m_batch.size())
        return;
    const QByteArray key = m_batch.at(index).key;
    const quint64 generation = m_generation;
    QPointer<KeyScanner> guard(this);

    m_connection->sendCommand({QByteArrayLiteral("TYPE"), key},
                              [guard, generation, index](const RespValue &reply,
                                                         const QString &error) {
        if (!guard || guard->m_generation != generation)
            return;
        if (index >= 0 && index < guard->m_batch.size()) {
            KeyInfo &info = guard->m_batch[index];
            if (error.isEmpty() && !reply.isError()) {
                info.type = QString::fromLatin1(reply.scalar);
            } else {
                info.type = QStringLiteral("unknown");
            }
        }
        guard->metadataDone();
    });

    m_connection->sendCommand({QByteArrayLiteral("PTTL"), key},
                              [guard, generation, index](const RespValue &reply,
                                                         const QString &error) {
        if (!guard || guard->m_generation != generation)
            return;
        if (index >= 0 && index < guard->m_batch.size()) {
            KeyInfo &info = guard->m_batch[index];
            if (error.isEmpty() && reply.type == RespType::Integer)
                info.ttlMs = reply.integer;
            else
                info.ttlMs = -2;
        }
        guard->metadataDone();
    });
}

void KeyScanner::metadataDone()
{
    if (m_metadataRemaining > 0)
        --m_metadataRemaining;
    if (m_metadataRemaining == 0)
        finishBatch();
}

void KeyScanner::finishBatch()
{
    QVector<KeyInfo> result;
    result.reserve(m_batch.size());
    for (const KeyInfo &info : std::as_const(m_batch)) {
        if (!info.isValid())
            continue;
        if (!m_useType && !m_type.isEmpty() && info.type != m_type)
            continue;
        result.append(info);
    }

    m_scanned += m_batch.size();
    m_cursor = m_nextCursor;
    m_finished = m_cursor == QByteArrayLiteral("0");
    m_running = false;
    m_batch.clear();
    m_batchIndex.clear();
    m_metadataRemaining = 0;

    emit batchReady(result);
    emit progress(m_scanned, m_finished);
    if (m_autoContinue && !m_finished)
        QTimer::singleShot(0, this, &KeyScanner::loadMore);
}

void KeyScanner::fail(const QString &error)
{
    m_running = false;
    m_finished = true;
    m_batch.clear();
    m_batchIndex.clear();
    m_metadataRemaining = 0;
    emit errorOccurred(error);
    emit progress(m_scanned, true);
}

} // namespace KeyDis
