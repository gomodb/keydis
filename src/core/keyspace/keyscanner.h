// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include "core/net/redisclient.h"

#include "core/keyspace/keyinfo.h"
#include "core/resp/respvalue.h"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

namespace KeyDis {


class KeyScanner : public QObject
{
    Q_OBJECT

public:
    explicit KeyScanner(RedisClient *connection, QObject *parent = nullptr);

    void start(const QByteArray &pattern = QByteArrayLiteral("*"),
               const QString &type = QString());
    void startAll(const QByteArray &pattern = QByteArrayLiteral("*"),
                  const QString &type = QString());
    void loadMore();
    void stop();

    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] bool isFinished() const;
    [[nodiscard]] qint64 scanned() const;

signals:
    void batchReady(const QVector<KeyInfo> &keys);
    void progress(qint64 scanned, bool finished);
    void errorOccurred(const QString &error);

private:
    void sendScan();
    void handleScan(const RespValue &reply, const QString &error);
    void requestMetadata(int index);
    void metadataDone();
    void finishBatch();
    void fail(const QString &error);

    QPointer<RedisClient> m_connection;
    QByteArray m_pattern = QByteArrayLiteral("*");
    QString m_type;
    QByteArray m_cursor = QByteArrayLiteral("0");
    QByteArray m_nextCursor = QByteArrayLiteral("0");
    int m_count = 500;
    bool m_useType = true;
    bool m_triedWithoutType = false;
    bool m_running = false;
    bool m_finished = false;
    bool m_autoContinue = false;
    quint64 m_generation = 0;
    qint64 m_scanned = 0;

    QVector<KeyInfo> m_batch;
    QHash<QByteArray, int> m_batchIndex;
    int m_metadataRemaining = 0;
};

} // namespace KeyDis
