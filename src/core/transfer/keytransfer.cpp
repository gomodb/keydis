// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "core/transfer/keytransfer.h"

#include "core/net/redisconnection.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <utility>

namespace KeyDis {

struct KeyTransfer::ExportPage {
    QByteArray nextCursor;
    QVector<QByteArray> keys;
    QVector<QByteArray> dumps;
    QVector<qint64> ttls;
    int remaining = 0;
};

KeyTransfer::KeyTransfer(RedisConnection *connection, QObject *parent)
    : QObject(parent)
    , m_connection(connection)
{
}

KeyTransfer::~KeyTransfer() = default;

void KeyTransfer::exportToFile(const QString &filePath, const QByteArray &pattern)
{
    if (m_running || !m_connection || !m_connection->isReady()) {
        emit finished(false, tr("Not connected"));
        return;
    }
    m_output = std::make_unique<QSaveFile>(filePath);
    if (!m_output->open(QIODevice::WriteOnly)) {
        const QString error = m_output->errorString();
        m_output.reset();
        emit finished(false, error);
        return;
    }
    ++m_generation;
    m_running = true;
    m_importing = false;
    m_cancelled = false;
    m_pattern = pattern.isEmpty() ? QByteArrayLiteral("*") : pattern;
    m_cursor = QByteArrayLiteral("0");
    m_count = 0;
    emit progress(0, 0, tr("Scanning keys..."));
    scanNext();
}

void KeyTransfer::scanNext()
{
    if (!m_running || m_cancelled)
        return;
    const quint64 generation = m_generation;
    QPointer<KeyTransfer> guard(this);
    m_connection->sendCommand({QByteArrayLiteral("SCAN"), m_cursor, QByteArrayLiteral("MATCH"),
                               m_pattern, QByteArrayLiteral("COUNT"), QByteArrayLiteral("500")},
                              [guard, generation](const RespValue &reply, const QString &error) {
        if (!guard || generation != guard->m_generation)
            return;
        if (!error.isEmpty() || reply.isError() || reply.type != RespType::Array
            || reply.elements.size() != 2) {
            guard->finish(false, error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
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
                guard->finish(true, tr("Export complete: %1 keys").arg(guard->m_count));
            } else {
                guard->m_cursor = nextCursor;
                guard->scanNext();
            }
            return;
        }
        guard->dumpPage(keys, nextCursor);
    });
}

void KeyTransfer::dumpPage(const QVector<QByteArray> &keys, const QByteArray &nextCursor)
{
    if (!m_running || m_cancelled)
        return;
    auto page = std::make_shared<ExportPage>();
    page->nextCursor = nextCursor;
    page->keys = keys;
    page->dumps.resize(keys.size());
    page->ttls.fill(-1, keys.size());
    page->remaining = int(keys.size()) * 2;

    const quint64 generation = m_generation;
    QPointer<KeyTransfer> guard(this);
    for (int index = 0; index < keys.size(); ++index) {
        m_connection->sendCommand({QByteArrayLiteral("DUMP"), keys.at(index)},
                                  [guard, generation, page, index](const RespValue &reply,
                                                                   const QString &error) {
            if (!guard || generation != guard->m_generation)
                return;
            if (error.isEmpty() && !reply.isError())
                page->dumps[index] = reply.scalar;
            if (--page->remaining == 0)
                guard->writePage(page);
        });
        m_connection->sendCommand({QByteArrayLiteral("PTTL"), keys.at(index)},
                                  [guard, generation, page, index](const RespValue &reply,
                                                                   const QString &error) {
            if (!guard || generation != guard->m_generation)
                return;
            if (error.isEmpty() && reply.type == RespType::Integer)
                page->ttls[index] = reply.integer;
            if (--page->remaining == 0)
                guard->writePage(page);
        });
    }
}

void KeyTransfer::writePage(std::shared_ptr<ExportPage> page)
{
    if (!m_running || m_cancelled || !m_output)
        return;
    for (int index = 0; index < page->keys.size(); ++index) {
        if (page->dumps.at(index).isEmpty())
            continue;
        QJsonObject object;
        object.insert(QStringLiteral("key"), QString::fromLatin1(page->keys.at(index).toBase64()));
        object.insert(QStringLiteral("dump"),
                      QString::fromLatin1(page->dumps.at(index).toBase64()));
        object.insert(QStringLiteral("ttl"), page->ttls.at(index));
        const QByteArray line = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
        if (m_output->write(line) < 0) {
            finish(false, m_output->errorString());
            return;
        }
        ++m_count;
    }
    emit progress(m_count, 0, tr("Exported %1 keys").arg(m_count));
    if (page->nextCursor == QByteArrayLiteral("0")) {
        finish(true, tr("Export complete: %1 keys").arg(m_count));
    } else {
        m_cursor = page->nextCursor;
        scanNext();
    }
}

void KeyTransfer::importFromFile(const QString &filePath)
{
    if (m_running) {
        emit finished(false, tr("Another transfer is running"));
        return;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit finished(false, file.errorString());
        return;
    }

    m_importLines.clear();
    m_importIndex = 0;
    m_count = 0;
    m_importing = true;
    m_running = true;
    m_cancelled = false;
    ++m_generation;
    for (const QByteArray &line : file.readAll().split('\n')) {
        if (!line.trimmed().isEmpty())
            m_importLines.append(line);
    }
    if (m_importLines.isEmpty()) {
        finish(true, tr("Nothing to import"));
        return;
    }
    emit progress(0, m_importLines.size(), tr("Importing..."));
    restoreNext();
}

void KeyTransfer::restoreNext()
{
    if (!m_running || m_cancelled)
        return;
    if (m_importIndex >= m_importLines.size()) {
        finish(true, tr("Import complete: %1 keys").arg(m_count));
        return;
    }

    const QByteArray line = m_importLines.at(m_importIndex);
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        finish(false, tr("Invalid import line %1").arg(m_importIndex + 1));
        return;
    }
    const QJsonObject object = document.object();
    const QByteArray key = QByteArray::fromBase64(object.value(QStringLiteral("key")).toString().toLatin1());
    const QByteArray dump = QByteArray::fromBase64(object.value(QStringLiteral("dump")).toString().toLatin1());
    const qint64 ttl = object.value(QStringLiteral("ttl")).toInteger(-1);
    if (key.isEmpty() || dump.isEmpty()) {
        finish(false, tr("Invalid import line %1").arg(m_importIndex + 1));
        return;
    }

    const quint64 generation = m_generation;
    QPointer<KeyTransfer> guard(this);
    m_connection->sendCommand({QByteArrayLiteral("RESTORE"), key,
                               QByteArray::number(ttl > 0 ? ttl : 0), dump,
                               QByteArrayLiteral("REPLACE")},
                              [guard, generation](const RespValue &reply, const QString &error) {
        if (!guard || generation != guard->m_generation)
            return;
        if (!error.isEmpty() || reply.isError()) {
            guard->finish(false, error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
            return;
        }
        ++guard->m_count;
        ++guard->m_importIndex;
        emit guard->progress(guard->m_count, guard->m_importLines.size(), tr("Importing..."));
        guard->restoreNext();
    });
}

void KeyTransfer::cancel()
{
    if (!m_running)
        return;
    m_cancelled = true;
    ++m_generation;
    if (m_output && m_importing == false)
        m_output->cancelWriting();
    finish(false, tr("Transfer cancelled"));
}

bool KeyTransfer::isRunning() const
{
    return m_running;
}

void KeyTransfer::finish(bool success, const QString &message)
{
    if (!m_running)
        return;
    m_running = false;
    m_cancelled = false;
    if (m_output) {
        if (success)
            m_output->commit();
        else
            m_output->cancelWriting();
        m_output.reset();
    }
    emit finished(success, message);
}

} // namespace KeyDis
