// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

#include <memory>

namespace KeyDis {

class RedisConnection;

struct KeyMemory {
    QByteArray key;
    QString type;
    qint64 bytes = 0;
    qint64 ttlMs = -1;
};

struct MemoryAnalysisResult {
    QVector<KeyMemory> keys;
    qint64 totalBytes = 0;
    int keyCount = 0;
    QHash<QString, qint64> bytesByType;
    QHash<QString, int> countByType;
};

class MemoryAnalyzer : public QObject
{
    Q_OBJECT

public:
    explicit MemoryAnalyzer(RedisConnection *connection, QObject *parent = nullptr);
    ~MemoryAnalyzer() override;

    void start(const QByteArray &pattern = QByteArrayLiteral("*"),
               const QString &type = QString());
    void cancel();

    [[nodiscard]] bool isRunning() const;

signals:
    void progress(qint64 scanned);
    void finished(bool success, const MemoryAnalysisResult &result, const QString &error);

private:
    struct Page;
    void scanNext();
    void analyzePage(const QVector<QByteArray> &keys, const QByteArray &nextCursor);
    void processPage(std::shared_ptr<Page> page);

    QPointer<RedisConnection> m_connection;
    QByteArray m_pattern;
    QString m_typeFilter;
    QByteArray m_cursor;
    MemoryAnalysisResult m_result;
    qint64 m_scanned = 0;
    bool m_running = false;
    bool m_cancelled = false;
    quint64 m_generation = 0;
};

} // namespace KeyDis
