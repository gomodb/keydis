// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "core/net/clusterclient.h"

#include "core/net/redisconnection.h"

#include <QMap>
#include <QSet>
#include <QTimer>
#include <QUuid>

#include <algorithm>

namespace KeyDis {

namespace {

quint16 crc16(const QByteArray &data)
{
    quint16 crc = 0;
    for (const char byte : data) {
        crc ^= static_cast<quint16>(static_cast<unsigned char>(byte)) << 8;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000) ? static_cast<quint16>((crc << 1) ^ 0x1021)
                                 : static_cast<quint16>(crc << 1);
    }
    return crc;
}

QByteArray hashTag(const QByteArray &key)
{
    const int open = key.indexOf('{');
    if (open < 0)
        return key;
    const int close = key.indexOf('}', open + 1);
    if (close < 0 || close == open + 1)
        return key;
    return key.mid(open + 1, close - open - 1);
}

bool splitEndpoint(const QString &text, QString *host, quint16 *port)
{
    const int colon = text.lastIndexOf(QLatin1Char(':'));
    if (colon <= 0 || colon == text.size() - 1)
        return false;
    QString candidate = text.left(colon);
    if (candidate.startsWith(QLatin1Char('[')) && candidate.endsWith(QLatin1Char(']')))
        candidate = candidate.mid(1, candidate.size() - 2);
    bool ok = false;
    const int value = text.mid(colon + 1).toInt(&ok);
    if (!ok || value <= 0 || value > 65535)
        return false;
    *host = candidate;
    *port = static_cast<quint16>(value);
    return true;
}

bool movedError(const RespValue &reply, bool *asking, int *slot, QString *host, quint16 *port)
{
    if (!reply.isError())
        return false;
    const QByteArray message = reply.scalar;
    if (message.startsWith("MOVED "))
        *asking = false;
    else if (message.startsWith("ASK "))
        *asking = true;
    else
        return false;

    const QList<QByteArray> parts = message.split(' ');
    if (parts.size() < 3)
        return false;
    bool slotOk = false;
    const int parsedSlot = parts.at(1).toInt(&slotOk);
    if (!slotOk || parsedSlot < 0 || parsedSlot >= 16384)
        return false;
    *slot = parsedSlot;
    return splitEndpoint(QString::fromUtf8(parts.at(2)), host, port);
}

} // namespace

ClusterClient::ClusterClient(QObject *parent)
    : RedisClient(parent)
{
    m_slots.fill(-1, 16384);
}

ClusterClient::~ClusterClient()
{
    disconnectFromHost();
}

void ClusterClient::connectToHost(const QString &host, quint16 port, bool tls,
                                  const QString &username, const QString &password, int db)
{
    Q_UNUSED(db)
    connectToCluster({QStringLiteral("%1:%2").arg(host).arg(port)}, tls, username, password);
}

void ClusterClient::setTlsConfig(const TlsConfig &config)
{
    m_tlsConfig = config;
}

void ClusterClient::connectToCluster(const QStringList &seeds, bool tls,
                                     const QString &username, const QString &password)
{
    disconnectFromHost();
    ++m_generation;
    m_seeds = seeds;
    m_tls = tls;
    m_username = username;
    m_password = password;
    m_seedIndex = 0;
    m_topologyRequested = false;
    m_connecting = true;
    m_ready = false;
    if (m_seeds.isEmpty()) {
        failCluster(tr("No cluster seed provided"));
        return;
    }
    emit stateChanged(tr("Connecting to cluster..."));
    connectSeed();
}

void ClusterClient::connectSeed()
{
    if (m_seedIndex >= m_seeds.size()) {
        failCluster(tr("Could not connect to any cluster seed"));
        return;
    }
    QString host;
    quint16 port = 0;
    if (!splitEndpoint(m_seeds.at(m_seedIndex), &host, &port)) {
        ++m_seedIndex;
        connectSeed();
        return;
    }

    m_seed = std::make_unique<RedisConnection>();
    m_seed->setTlsConfig(m_tlsConfig);
    m_topologyRequested = false;
    const int generation = static_cast<int>(m_generation);
    connect(m_seed.get(), &RedisClient::stateChanged, this,
            [this, generation](const QString &state) {
        if (generation != static_cast<int>(m_generation))
            return;
        if (state != QLatin1String("Ready") && state != QLatin1String("Ready (RESP2)"))
            return;
        if (!m_topologyRequested)
            requestTopology();
    });
    connect(m_seed.get(), &RedisClient::errorOccurred, this,
            [this, generation](const QString &error) {
        if (generation != static_cast<int>(m_generation) || m_topologyRequested)
            return;
        if (++m_seedIndex < m_seeds.size()) {
            QTimer::singleShot(0, this, &ClusterClient::connectSeed);
        } else {
            failCluster(error);
        }
    });
    m_seed->connectToHost(host, port, m_tls, m_username, m_password, 0);
}

void ClusterClient::requestTopology()
{
    m_topologyRequested = true;
    emit stateChanged(tr("Reading cluster topology..."));
    const int generation = static_cast<int>(m_generation);
    QPointer<ClusterClient> guard(this);
    m_seed->sendCommand({QByteArrayLiteral("CLUSTER"), QByteArrayLiteral("SLOTS")},
                        [guard, generation](const RespValue &reply, const QString &error) {
        if (!guard || generation != static_cast<int>(guard->m_generation))
            return;
        if (!error.isEmpty() || reply.isError() || reply.type != RespType::Array) {
            guard->failCluster(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
            return;
        }
        guard->applyTopology(reply);
    });
}

void ClusterClient::applyTopology(const RespValue &reply)
{
    clearNodes();
    m_slots.fill(-1, 16384);

    for (const RespValue &range : reply.elements) {
        if (range.type != RespType::Array || range.elements.size() < 3)
            continue;
        const qint64 start = range.elements[0].integer;
        const qint64 end = range.elements[1].integer;
        const RespValue &master = range.elements[2];
        if (master.type != RespType::Array || master.elements.size() < 2)
            continue;
        QString host = QString::fromUtf8(master.elements[0].scalar);
        if (host.isEmpty())
            host = QStringLiteral("127.0.0.1");
        const qint64 port = master.elements[1].integer != 0
            ? master.elements[1].integer
            : QString::fromUtf8(master.elements[1].scalar).toLongLong();
        if (port <= 0 || port > 65535 || start < 0 || end >= 16384 || start > end)
            continue;
        const QString id = master.elements.size() > 2
            ? QString::fromUtf8(master.elements[2].scalar)
            : QString();
        createNode(host, static_cast<quint16>(port), id);
        if (m_nodes.empty())
            continue;
        const int nodeIndex = m_nodes.back()->index;
        for (qint64 slot = start; slot <= end; ++slot)
            m_slots[static_cast<int>(slot)] = nodeIndex;
    }

    if (m_nodes.empty() || std::all_of(m_slots.cbegin(), m_slots.cend(),
                                         [](int value) { return value < 0; })) {
        failCluster(tr("Cluster returned an empty slot map"));
        return;
    }

    emit stateChanged(tr("Connecting to %1 cluster nodes...").arg(static_cast<int>(m_nodes.size())));
    connectNodes();
}

void ClusterClient::createNode(const QString &host, quint16 port, const QString &id)
{
    if (findNode(host, port))
        return;
    auto node = std::make_unique<Node>();
    node->host = host;
    node->port = port;
    node->id = id;
    node->index = static_cast<int>(m_nodes.size());
    node->connection = std::make_unique<RedisConnection>();
    node->connection->setTlsConfig(m_tlsConfig);
    Node *raw = node.get();
    m_nodes.push_back(std::move(node));

    connect(raw->connection.get(), &RedisClient::stateChanged, this,
            [this, raw](const QString &state) {
        if (state != QLatin1String("Ready") && state != QLatin1String("Ready (RESP2)"))
            return;
        raw->ready = true;
        const auto queued = raw->queued;
        raw->queued.clear();
        for (const auto &request : queued)
            sendToNode(raw, request.args, request.callback, 0, request.asking);
        maybeReady();
    });
    connect(raw->connection.get(), &RedisClient::errorOccurred, this,
            [this](const QString &error) {
        if (!m_ready) {
            QTimer::singleShot(0, this, [this, error] {
                if (!m_ready && m_connecting)
                    failCluster(error);
            });
        } else {
            emit errorOccurred(error);
        }
    });
    connect(raw->connection.get(), &RedisClient::replyReady, this,
            [this](const RespValue &reply) { emit replyReady(reply); });
    connect(raw->connection.get(), &RedisClient::pushReceived, this,
            [this](const RespValue &reply) { emit pushReceived(reply); });
}

void ClusterClient::connectNodes()
{
    for (const auto &node : m_nodes)
        node->connection->connectToHost(node->host, node->port, m_tls, m_username, m_password, 0);
}

void ClusterClient::maybeReady()
{
    if (m_ready || m_nodes.empty())
        return;
    for (const auto &node : m_nodes) {
        if (!node->ready)
            return;
    }
    m_ready = true;
    m_connecting = false;
    emit stateChanged(tr("Ready"));
    flushPending();
}

void ClusterClient::flushPending()
{
    const auto pending = m_pending;
    m_pending.clear();
    for (const auto &request : pending)
        sendCommand(request.args, request.callback);
}

void ClusterClient::disconnectFromHost()
{
    ++m_generation;
    m_ready = false;
    m_connecting = false;
    m_topologyRequested = false;
    for (auto &request : m_pending) {
        if (request.callback)
            request.callback(RespValue{}, tr("Disconnected"));
    }
    m_pending.clear();
    m_scans.clear();
    clearNodes();
    m_seed.reset();
    emit stateChanged(tr("Disconnected"));
}

bool ClusterClient::isReady() const
{
    return m_ready;
}

void ClusterClient::sendCommand(const QList<QByteArray> &args, ReplyCallback callback)
{
    if (args.isEmpty()) {
        if (callback)
            callback(RespValue{}, tr("Empty command"));
        return;
    }
    if (!m_ready) {
        if (m_connecting)
            m_pending.append({args, std::move(callback)});
        else if (callback)
            callback(RespValue{}, tr("Cluster is not connected"));
        return;
    }

    const QByteArray command = args.first().toUpper();
    if (command == QByteArrayLiteral("SCAN")) {
        sendClusterScan(args, std::move(callback));
        return;
    }
    if (command == QByteArrayLiteral("INFO")) {
        sendClusterInfo(args, std::move(callback));
        return;
    }

    const QVector<int> indexes = keyIndexes(command, args);
    if (indexes.isEmpty()) {
        Node *node = m_nodes.empty() ? nullptr : m_nodes.front().get();
        if (!node) {
            if (callback)
                callback(RespValue{}, tr("No cluster node available"));
            return;
        }
        sendToNode(node, args, std::move(callback), 0, false);
        return;
    }

    int slot = -1;
    for (const int index : indexes) {
        if (index <= 0 || index >= args.size())
            continue;
        const int candidate = slotForKey(args.at(index));
        if (slot < 0)
            slot = candidate;
        else if (slot != candidate) {
            if (callback)
                callback(RespValue{}, QStringLiteral(
                                           "CROSSSLOT Keys in request don't hash to the same slot"));
            return;
        }
    }
    if (slot < 0) {
        Node *node = m_nodes.empty() ? nullptr : m_nodes.front().get();
        if (node)
            sendToNode(node, args, std::move(callback), 0, false);
        else if (callback)
            callback(RespValue{}, tr("No cluster node available"));
        return;
    }

    Node *node = nodeForSlot(slot);
    if (!node) {
        if (callback)
            callback(RespValue{}, tr("No node owns slot %1").arg(slot));
        return;
    }
    sendToNode(node, args, std::move(callback), 0, false);
}

void ClusterClient::sendTransaction(const QList<QList<QByteArray>> &commands,
                                    ReplyCallback callback)
{
    if (commands.isEmpty()) {
        RespValue empty;
        empty.type = RespType::Array;
        if (callback)
            callback(empty, {});
        return;
    }
    if (!m_ready) {
        if (callback)
            callback(RespValue{}, tr("Cluster is not connected"));
        return;
    }

    int slot = -1;
    for (const QList<QByteArray> &args : commands) {
        if (args.isEmpty())
            continue;
        const QVector<int> indexes = keyIndexes(args.first().toUpper(), args);
        for (const int index : indexes) {
            if (index <= 0 || index >= args.size())
                continue;
            const int candidate = slotForKey(args.at(index));
            if (slot < 0)
                slot = candidate;
            else if (slot != candidate) {
                if (callback)
                    callback(RespValue{}, QStringLiteral(
                        "CROSSSLOT Keys in request don't hash to the same slot"));
                return;
            }
        }
    }

    Node *node = slot >= 0 ? nodeForSlot(slot)
                          : (m_nodes.empty() ? nullptr : m_nodes.front().get());
    if (!node || !node->connection) {
        if (callback)
            callback(RespValue{}, tr("No cluster node available"));
        return;
    }
    if (!node->ready) {
        if (callback)
            callback(RespValue{}, tr("Cluster node is unavailable"));
        return;
    }
    node->connection->sendTransaction(commands, std::move(callback));
}

QVector<int> ClusterClient::keyIndexes(const QByteArray &command,
                                       const QList<QByteArray> &args) const
{
    static const QSet<QByteArray> keyless{
        QByteArrayLiteral("PING"), QByteArrayLiteral("INFO"), QByteArrayLiteral("TIME"),
        QByteArrayLiteral("DBSIZE"), QByteArrayLiteral("CLUSTER"), QByteArrayLiteral("CLIENT"),
        QByteArrayLiteral("SLOWLOG"), QByteArrayLiteral("CONFIG"), QByteArrayLiteral("COMMAND"),
        QByteArrayLiteral("PUBLISH"), QByteArrayLiteral("SUBSCRIBE"),
        QByteArrayLiteral("UNSUBSCRIBE"), QByteArrayLiteral("PSUBSCRIBE"),
        QByteArrayLiteral("PUNSUBSCRIBE"), QByteArrayLiteral("MONITOR"),
        QByteArrayLiteral("AUTH"), QByteArrayLiteral("HELLO"), QByteArrayLiteral("QUIT"),
        QByteArrayLiteral("SCRIPT"), QByteArrayLiteral("FUNCTION"),
        QByteArrayLiteral("ACL"), QByteArrayLiteral("LATENCY"), QByteArrayLiteral("MODULE"),
        QByteArrayLiteral("LASTSAVE"), QByteArrayLiteral("SAVE"), QByteArrayLiteral("BGSAVE"),
        QByteArrayLiteral("BGREWRITEAOF"), QByteArrayLiteral("FLUSHALL"),
        QByteArrayLiteral("FLUSHDB"), QByteArrayLiteral("RANDOMKEY"), QByteArrayLiteral("KEYS"),
        QByteArrayLiteral("PUBSUB"), QByteArrayLiteral("SELECT"),
    };
    if (keyless.contains(command))
        return {};

    if (command == QByteArrayLiteral("MEMORY") && args.size() > 1
        && args.at(1).toUpper() == QByteArrayLiteral("USAGE"))
        return {2};

    if (command == QByteArrayLiteral("EVAL") || command == QByteArrayLiteral("EVALSHA")
        || command == QByteArrayLiteral("FCALL") || command == QByteArrayLiteral("FCALL_RO")) {
        if (args.size() < 3)
            return {};
        bool ok = false;
        const int count = args.at(2).toInt(&ok);
        if (!ok || count <= 0)
            return {};
        QVector<int> indexes;
        for (int index = 0; index < count && 3 + index < args.size(); ++index)
            indexes.append(3 + index);
        return indexes;
    }

    if (command == QByteArrayLiteral("MSET") || command == QByteArrayLiteral("MSETNX")) {
        QVector<int> indexes;
        for (int index = 1; index < args.size(); index += 2)
            indexes.append(index);
        return indexes;
    }

    if (command == QByteArrayLiteral("DEL") || command == QByteArrayLiteral("UNLINK")
        || command == QByteArrayLiteral("EXISTS") || command == QByteArrayLiteral("TOUCH")
        || command == QByteArrayLiteral("MGET")) {
        QVector<int> indexes;
        for (int index = 1; index < args.size(); ++index)
            indexes.append(index);
        return indexes;
    }

    if (command == QByteArrayLiteral("RENAME") || command == QByteArrayLiteral("RENAMENX")
        || command == QByteArrayLiteral("SMOVE") || command == QByteArrayLiteral("LMOVE")
        || command == QByteArrayLiteral("RPOPLPUSH"))
        return {1, 2};

    if (command == QByteArrayLiteral("BITOP")) {
        QVector<int> indexes;
        for (int index = 2; index < args.size(); ++index)
            indexes.append(index);
        return indexes;
    }

    if (command == QByteArrayLiteral("ZUNIONSTORE") || command == QByteArrayLiteral("ZINTERSTORE")
        || command == QByteArrayLiteral("ZDIFFSTORE")) {
        if (args.size() < 3)
            return {1};
        bool ok = false;
        const int count = args.at(2).toInt(&ok);
        QVector<int> indexes{1};
        if (ok) {
            for (int index = 0; index < count && 3 + index < args.size(); ++index)
                indexes.append(3 + index);
        }
        return indexes;
    }

    if (command == QByteArrayLiteral("XREAD") || command == QByteArrayLiteral("XREADGROUP")) {
        const int streams = args.indexOf(QByteArrayLiteral("STREAMS"));
        if (streams >= 0 && streams + 1 < args.size())
            return {streams + 1};
        return {1};
    }

    return {1};
}

int ClusterClient::slotForKey(const QByteArray &key) const
{
    return static_cast<int>(crc16(hashTag(key)) % 16384);
}

ClusterClient::Node *ClusterClient::nodeForSlot(int slot) const
{
    if (slot < 0 || slot >= m_slots.size())
        return nullptr;
    const int index = m_slots.at(slot);
    if (index < 0 || index >= static_cast<int>(m_nodes.size()))
        return nullptr;
    return m_nodes.at(index).get();
}

ClusterClient::Node *ClusterClient::findNode(const QString &host, quint16 port) const
{
    for (const auto &node : m_nodes) {
        if (node->host == host && node->port == port)
            return node.get();
    }
    return nullptr;
}

void ClusterClient::sendToNode(Node *node, const QList<QByteArray> &args,
                               ReplyCallback callback, int attempt, bool asking)
{
    if (!node || !node->connection) {
        if (callback)
            callback(RespValue{}, tr("Cluster node is unavailable"));
        return;
    }
    if (!node->ready) {
        node->queued.append({args, std::move(callback), asking});
        return;
    }
    if (attempt > 3) {
        if (callback)
            callback(RespValue{}, tr("Too many cluster redirections"));
        return;
    }

    QPointer<ClusterClient> guard(this);
    if (asking) {
        node->connection->sendCommand({QByteArrayLiteral("ASKING")},
                                      [guard, node, args, callback, attempt](const RespValue &reply,
                                                                             const QString &error) {
            if (!guard)
                return;
            if (!error.isEmpty() || reply.isError()) {
                if (callback)
                    callback(reply, error);
                return;
            }
            guard->sendToNode(node, args, std::move(callback), attempt, false);
        });
        return;
    }

    node->connection->sendCommand(args, [guard, node, args, callback, attempt](const RespValue &reply,
                                                                              const QString &error) {
        if (!guard)
            return;
        bool ask = false;
        int slot = 0;
        QString host;
        quint16 port = 0;
        if (movedError(reply, &ask, &slot, &host, &port)) {
            Node *target = guard->findNode(host, port);
            if (!target) {
                guard->createNode(host, port, {});
                target = guard->findNode(host, port);
                if (target)
                    target->connection->connectToHost(host, port, guard->m_tls, guard->m_username,
                                                      guard->m_password, 0);
            }
            if (!target) {
                if (callback)
                    callback(reply, {});
                return;
            }
            if (!ask)
                guard->m_slots[slot] = target->index;
            guard->sendToNode(target, args, std::move(callback), attempt + 1, ask);
            return;
        }
        if (callback)
            callback(reply, error);
    });
}

void ClusterClient::sendClusterInfo(const QList<QByteArray> &args, ReplyCallback callback)
{
    if (m_nodes.empty()) {
        if (callback)
            callback(RespValue{}, tr("No cluster node available"));
        return;
    }
    struct State {
        int remaining = 0;
        QStringList texts;
        QString error;
    };
    auto state = std::make_shared<State>();
    state->remaining = static_cast<int>(m_nodes.size());
    state->texts.resize(static_cast<int>(m_nodes.size()));
    QPointer<ClusterClient> guard(this);
    for (int index = 0; index < static_cast<int>(m_nodes.size()); ++index) {
        m_nodes.at(index)->connection->sendCommand(
            args, [guard, state, index, callback](const RespValue &reply, const QString &error) {
            if (!guard)
                return;
            if (!error.isEmpty() || reply.isError()) {
                if (state->error.isEmpty())
                    state->error = error.isEmpty() ? QString::fromUtf8(reply.scalar) : error;
            } else {
                state->texts[index] = QString::fromUtf8(reply.scalar);
            }
            if (--state->remaining == 0) {
                if (!state->error.isEmpty()) {
                    if (callback)
                        callback(RespValue{}, state->error);
                    return;
                }
                const QSet<QString> additive{
                    QStringLiteral("used_memory"), QStringLiteral("used_memory_rss"),
                    QStringLiteral("used_memory_peak"), QStringLiteral("used_memory_lua"),
                    QStringLiteral("used_memory_scripts"), QStringLiteral("used_memory_dataset"),
                    QStringLiteral("used_memory_startup"), QStringLiteral("used_memory_overhead"),
                    QStringLiteral("connected_clients"), QStringLiteral("cluster_connections"),
                    QStringLiteral("total_connections_received"),
                    QStringLiteral("total_commands_processed"),
                    QStringLiteral("instantaneous_ops_per_sec"),
                    QStringLiteral("rejected_connections"), QStringLiteral("expired_keys"),
                    QStringLiteral("evicted_keys"), QStringLiteral("keyspace_hits"),
                    QStringLiteral("keyspace_misses"), QStringLiteral("total_net_input_bytes"),
                    QStringLiteral("total_net_output_bytes"), QStringLiteral("used_cpu_sys"),
                    QStringLiteral("used_cpu_user"),
                };
                QMap<QString, qint64> totals;
                struct DbInfo {
                    qint64 keys = 0;
                    qint64 expires = 0;
                    qint64 avgTtl = 0;
                    bool seen = false;
                };
                QMap<QString, DbInfo> dbInfo;
                QStringList firstLines;
                for (int node = 0; node < state->texts.size(); ++node) {
                    const QStringList lines = state->texts.at(node).split(QLatin1Char('\n'));
                    for (const QString &raw : lines) {
                        const QString line = raw.trimmed();
                        if (line.isEmpty())
                            continue;
                        if (node == 0)
                            firstLines.append(line);
                        if (line.startsWith(QLatin1String("db")) && line.contains(QLatin1String(":keys="))) {
                            const int colon = line.indexOf(QLatin1Char(':'));
                            if (colon <= 0)
                                continue;
                            DbInfo info = dbInfo.value(line.left(colon));
                            QString statsText = line.mid(colon + 1);
                            statsText.replace(QLatin1Char(';'), QLatin1Char(','));
                            const QStringList parts = statsText.split(QLatin1Char(','));
                            for (const QString &part : parts) {
                                const int equals = part.indexOf(QLatin1Char('='));
                                if (equals <= 0)
                                    continue;
                                bool ok = false;
                                const qint64 value = part.mid(equals + 1).toLongLong(&ok);
                                if (!ok)
                                    continue;
                                if (part.startsWith(QLatin1String("keys=")))
                                    info.keys += value;
                                else if (part.startsWith(QLatin1String("expires=")))
                                    info.expires += value;
                                else if (part.startsWith(QLatin1String("avg_ttl=")))
                                    info.avgTtl = qMax(info.avgTtl, value);
                            }
                            info.seen = true;
                            dbInfo.insert(line.left(colon), info);
                            continue;
                        }
                        const int colon = line.indexOf(QLatin1Char(':'));
                        if (colon <= 0)
                            continue;
                        const QString key = line.left(colon);
                        if (!additive.contains(key))
                            continue;
                        bool ok = false;
                        const qint64 value = line.mid(colon + 1).toLongLong(&ok);
                        if (ok && value >= 0)
                            totals[key] += value;
                    }
                }

                QStringList output;
                bool keyspaceHeader = false;
                for (const QString &line : std::as_const(firstLines)) {
                    if (line.startsWith(QLatin1String("db")) && line.contains(QLatin1String(":keys=")))
                        continue;
                    const int colon = line.indexOf(QLatin1Char(':'));
                    if (colon > 0) {
                        const QString key = line.left(colon);
                        if (totals.contains(key)) {
                            output.append(key + QLatin1Char(':') + QString::number(totals.value(key)));
                            continue;
                        }
                    }
                    output.append(line);
                    if (line.startsWith(QLatin1String("# Keyspace"))) {
                        keyspaceHeader = true;
                        for (auto it = dbInfo.constBegin(); it != dbInfo.constEnd(); ++it) {
                            output.append(QStringLiteral("%1:keys=%2,expires=%3,avg_ttl=%4")
                                              .arg(it.key())
                                              .arg(it.value().keys)
                                              .arg(it.value().expires)
                                              .arg(it.value().avgTtl));
                        }
                    }
                }
                if (!keyspaceHeader && !dbInfo.isEmpty()) {
                    output.append(QStringLiteral("# Keyspace"));
                    for (auto it = dbInfo.constBegin(); it != dbInfo.constEnd(); ++it) {
                        output.append(QStringLiteral("%1:keys=%2,expires=%3,avg_ttl=%4")
                                          .arg(it.key())
                                          .arg(it.value().keys)
                                          .arg(it.value().expires)
                                          .arg(it.value().avgTtl));
                    }
                }
                RespValue result;
                result.type = RespType::BulkString;
                result.scalar = output.join(QLatin1Char('\n')).toUtf8() + '\n';
                if (callback)
                    callback(result, {});
            }
        });
    }
}

void ClusterClient::sendClusterScan(const QList<QByteArray> &args, ReplyCallback callback)
{
    if (args.size() < 2) {
        if (callback)
            callback(RespValue{}, tr("Invalid cluster SCAN"));
        return;
    }
    const QByteArray cursor = args.at(1);
    std::shared_ptr<ScanState> state;
    QByteArray token = cursor;
    if (cursor == QByteArrayLiteral("0")) {
        m_scans.clear();
        state = std::make_shared<ScanState>();
        state->cursors.fill(QByteArrayLiteral("0"), static_cast<int>(m_nodes.size()));
        state->options = args.mid(2);
        state->remaining = static_cast<int>(m_nodes.size());
        state->pending = static_cast<int>(m_nodes.size());
        if (m_nodes.empty()) {
            if (callback)
                callback(RespValue{}, tr("No cluster node available"));
            return;
        }
        for (int index = 0; index < static_cast<int>(m_nodes.size()); ++index) {
            QList<QByteArray> command{QByteArrayLiteral("SCAN"), QByteArrayLiteral("0")};
            command += state->options;
            QPointer<ClusterClient> guard(this);
            m_nodes.at(index)->connection->sendCommand(
                command, [guard, state, index, token, callback](const RespValue &reply,
                                                                const QString &error) mutable {
                if (!guard)
                    return;
                if (!error.isEmpty() || reply.isError() || reply.type != RespType::Array
                    || reply.elements.size() != 2) {
                    if (state->error.isEmpty())
                        state->error = error.isEmpty() ? QString::fromUtf8(reply.scalar) : error;
                } else {
                    state->cursors[index] = reply.elements[0].scalar;
                    for (const RespValue &key : reply.elements[1].elements) {
                        if (key.type == RespType::BulkString)
                            state->keys.append(key.scalar);
                    }
                }
                if (--state->pending == 0)
                    guard->finishScan(token, state, callback);
            });
        }
        return;
    }

    state = m_scans.value(cursor);
    if (!state) {
        if (callback)
            callback(RespValue{}, tr("Invalid cluster SCAN cursor"));
        return;
    }
    token = cursor;
    state->pending = 0;
    for (int index = 0; index < state->cursors.size() && index < static_cast<int>(m_nodes.size()); ++index) {
        if (state->cursors.at(index) == QByteArrayLiteral("0"))
            continue;
        ++state->pending;
        QList<QByteArray> command{QByteArrayLiteral("SCAN"), state->cursors.at(index)};
        command += state->options;
        QPointer<ClusterClient> guard(this);
        m_nodes.at(index)->connection->sendCommand(
            command, [guard, state, index, token, callback](const RespValue &reply,
                                                            const QString &error) mutable {
            if (!guard)
                return;
            if (!error.isEmpty() || reply.isError() || reply.type != RespType::Array
                || reply.elements.size() != 2) {
                if (state->error.isEmpty())
                    state->error = error.isEmpty() ? QString::fromUtf8(reply.scalar) : error;
            } else {
                state->cursors[index] = reply.elements[0].scalar;
                for (const RespValue &key : reply.elements[1].elements) {
                    if (key.type == RespType::BulkString)
                        state->keys.append(key.scalar);
                }
            }
            if (--state->pending == 0)
                guard->finishScan(token, state, callback);
        });
    }
    if (state->pending == 0)
        finishScan(token, state, callback);
}

void ClusterClient::finishScan(const QByteArray &token, const std::shared_ptr<ScanState> &state,
                               ReplyCallback callback)
{
    if (!state->error.isEmpty()) {
        if (token != QByteArrayLiteral("0"))
            m_scans.remove(token);
        if (callback)
            callback(RespValue{}, state->error);
        return;
    }

    bool more = false;
    for (const QByteArray &cursor : state->cursors) {
        if (cursor != QByteArrayLiteral("0")) {
            more = true;
            break;
        }
    }

    QByteArray nextCursor = QByteArrayLiteral("0");
    if (more) {
        if (token == QByteArrayLiteral("0")) {
            nextCursor = "cluster:" + QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8();
            m_scans.insert(nextCursor, state);
        } else {
            nextCursor = token;
        }
    } else if (token != QByteArrayLiteral("0")) {
        m_scans.remove(token);
    }

    RespValue cursorValue;
    cursorValue.type = RespType::BulkString;
    cursorValue.scalar = nextCursor;
    RespValue keysValue;
    keysValue.type = RespType::Array;
    for (const QByteArray &key : state->keys) {
        RespValue value;
        value.type = RespType::BulkString;
        value.scalar = key;
        keysValue.elements.push_back(std::move(value));
    }
    RespValue result;
    result.type = RespType::Array;
    result.elements.push_back(std::move(cursorValue));
    result.elements.push_back(std::move(keysValue));
    if (callback)
        callback(result, {});
}

void ClusterClient::failCluster(const QString &error)
{
    if (!m_ready && !m_connecting)
        return;
    m_ready = false;
    m_connecting = false;
    m_topologyRequested = false;
    for (auto &request : m_pending) {
        if (request.callback)
            request.callback(RespValue{}, error);
    }
    m_pending.clear();
    m_scans.clear();
    clearNodes();
    emit errorOccurred(error);
    emit stateChanged(tr("Disconnected"));
}

void ClusterClient::clearNodes()
{
    for (auto &node : m_nodes) {
        if (node->connection)
            node->connection->disconnectFromHost();
    }
    m_nodes.clear();
    m_slots.fill(-1, 16384);
}

} // namespace KeyDis
