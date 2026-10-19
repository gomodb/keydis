// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include "core/net/redisclient.h"

#include <QByteArray>
#include <QHash>
#include <QPointer>
#include <QStringList>
#include <QVector>

#include <memory>
#include <vector>

namespace KeyDis {

class RedisConnection;

class ClusterClient : public RedisClient
{
    Q_OBJECT

public:
    explicit ClusterClient(QObject *parent = nullptr);
    ~ClusterClient() override;

    void connectToHost(const QString &host, quint16 port, bool tls, const QString &username,
                       const QString &password, int db) override;
    void disconnectFromHost() override;
    [[nodiscard]] bool isReady() const override;
    void sendCommand(const QList<QByteArray> &args, ReplyCallback callback = {}) override;

    void connectToCluster(const QStringList &seeds, bool tls, const QString &username,
                          const QString &password);

private:
    struct Node {
        QString host;
        quint16 port = 0;
        QString id;
        std::unique_ptr<RedisConnection> connection;
        bool ready = false;
        int index = -1;
        struct Queued {
            QList<QByteArray> args;
            ReplyCallback callback;
            bool asking = false;
        };
        QVector<Queued> queued;
    };

    struct PendingRequest {
        QList<QByteArray> args;
        ReplyCallback callback;
    };

    struct ScanState {
        QVector<QByteArray> cursors;
        QVector<QByteArray> keys;
        QList<QByteArray> options;
        int pending = 0;
        int remaining = 0;
        QString error;
    };

    void connectSeed();
    void requestTopology();
    void applyTopology(const RespValue &reply);
    void connectNodes();
    void maybeReady();
    void flushPending();
    void failCluster(const QString &error);
    void clearNodes();
    void createNode(const QString &host, quint16 port, const QString &id);

    [[nodiscard]] Node *nodeForSlot(int slot) const;
    [[nodiscard]] int slotForKey(const QByteArray &key) const;
    [[nodiscard]] QVector<int> keyIndexes(const QByteArray &command,
                                          const QList<QByteArray> &args) const;
    void sendToNode(Node *node, const QList<QByteArray> &args, ReplyCallback callback,
                    int attempt, bool asking);
    void sendClusterScan(const QList<QByteArray> &args, ReplyCallback callback);
    void sendClusterInfo(const QList<QByteArray> &args, ReplyCallback callback);
    void finishScan(const QByteArray &token, const std::shared_ptr<ScanState> &state,
                    ReplyCallback callback);
    [[nodiscard]] Node *findNode(const QString &host, quint16 port) const;

    QStringList m_seeds;
    int m_seedIndex = 0;
    bool m_tls = false;
    QString m_username;
    QString m_password;
    std::unique_ptr<RedisConnection> m_seed;
    bool m_topologyRequested = false;
    bool m_ready = false;
    bool m_connecting = false;
    std::vector<std::unique_ptr<Node>> m_nodes;
    QVector<int> m_slots;
    QVector<PendingRequest> m_pending;
    QHash<QByteArray, std::shared_ptr<ScanState>> m_scans;
    quint64 m_generation = 0;
};

} // namespace KeyDis
