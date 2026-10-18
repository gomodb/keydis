// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include "core/net/redisclient.h"

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

#include <memory>

class QSaveFile;

namespace KeyDis {


class KeyTransfer : public QObject
{
    Q_OBJECT

public:
    explicit KeyTransfer(RedisClient *connection, QObject *parent = nullptr);
    ~KeyTransfer() override;

    void exportToFile(const QString &filePath, const QByteArray &pattern = QByteArrayLiteral("*"));
    void importFromFile(const QString &filePath);
    void cancel();

    [[nodiscard]] bool isRunning() const;

signals:
    void progress(qint64 current, qint64 total, const QString &phase);
    void finished(bool success, const QString &message);

private:
    struct ExportPage;
    void scanNext();
    void dumpPage(const QVector<QByteArray> &keys, const QByteArray &nextCursor);
    void writePage(std::shared_ptr<ExportPage> page);
    void finish(bool success, const QString &message);
    void restoreNext();

    QPointer<RedisClient> m_connection;
    std::unique_ptr<QSaveFile> m_output;
    QByteArray m_pattern;
    QByteArray m_cursor;
    QVector<QByteArray> m_importLines;
    qsizetype m_importIndex = 0;
    qint64 m_count = 0;
    bool m_running = false;
    bool m_importing = false;
    bool m_cancelled = false;
    quint64 m_generation = 0;
};

} // namespace KeyDis
