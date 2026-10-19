// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "ui/tools/servertools.h"

#include "core/analysis/memoryanalyzer.h"
#include "core/net/redisclient.h"
#include "core/net/redisconnection.h"
#include "core/transfer/keytransfer.h"

#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QInputDialog>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QVBoxLayout>

namespace KeyDis {

namespace {

QTableWidgetItem *item(const QString &text)
{
    auto *result = new QTableWidgetItem(text);
    result->setFlags(result->flags() & ~Qt::ItemIsEditable);
    return result;
}

QString argsText(const RespValue &args)
{
    QStringList parts;
    for (const RespValue &arg : args.elements)
        parts.append(QString::fromUtf8(arg.scalar));
    return parts.join(QLatin1Char(' '));
}

QString formatBytes(qint64 bytes)
{
    if (bytes < 1024)
        return QObject::tr("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QObject::tr("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024LL * 1024 * 1024)
        return QObject::tr("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
    return QObject::tr("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

QString formatTtl(qint64 ttlMs)
{
    if (ttlMs < 0)
        return QObject::tr("persistent");
    if (ttlMs < 1000)
        return QObject::tr("%1 ms").arg(ttlMs);
    return QObject::tr("%1 s").arg(ttlMs / 1000);
}

bool parseNodeAddress(const QString &address, QString *host, quint16 *port)
{
    QString value = address.section(QLatin1Char('@'), 0, 0);
    const int colon = value.lastIndexOf(QLatin1Char(':'));
    if (colon <= 0 || colon == value.size() - 1)
        return false;
    QString candidate = value.left(colon);
    if (candidate.startsWith(QLatin1Char('[')) && candidate.endsWith(QLatin1Char(']')))
        candidate = candidate.mid(1, candidate.size() - 2);
    bool ok = false;
    const int parsed = value.mid(colon + 1).toInt(&ok);
    if (!ok || parsed <= 0 || parsed > 65535)
        return false;
    *host = candidate;
    *port = static_cast<quint16>(parsed);
    return true;
}

QTableWidget *makeTable(const QStringList &headers, QWidget *parent)
{
    auto *table = new QTableWidget(0, headers.size(), parent);
    table->setHorizontalHeaderLabels(headers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    return table;
}

} // namespace

ServerToolsPage::ServerToolsPage(RedisClient *connection, QWidget *parent)
    : QWidget(parent)
    , m_connection(connection)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    m_tabs = new QTabWidget(this);

    auto *slowlog = new QWidget(m_tabs);
    auto *slowlogLayout = new QVBoxLayout(slowlog);
    auto *slowlogBar = new QHBoxLayout;
    slowlogBar->addWidget(new QLabel(tr("Redis SLOWLOG entries"), slowlog));
    slowlogBar->addStretch();
    auto *slowlogRefresh = new QPushButton(tr("Refresh"), slowlog);
    slowlogBar->addWidget(slowlogRefresh);
    slowlogLayout->addLayout(slowlogBar);
    m_slowlogTable = makeTable({tr("ID"), tr("Time"), tr("Duration"), tr("Command")}, slowlog);
    slowlogLayout->addWidget(m_slowlogTable, 1);
    connect(slowlogRefresh, &QPushButton::clicked, this, &ServerToolsPage::refreshSlowlog);
    m_tabs->addTab(slowlog, tr("Slowlog"));

    auto *clients = new QWidget(m_tabs);
    auto *clientsLayout = new QVBoxLayout(clients);
    auto *clientsBar = new QHBoxLayout;
    clientsBar->addWidget(new QLabel(tr("Connected clients"), clients));
    clientsBar->addStretch();
    auto *clientsRefresh = new QPushButton(tr("Refresh"), clients);
    clientsBar->addWidget(clientsRefresh);
    clientsLayout->addLayout(clientsBar);
    m_clientsTable = makeTable(
        {tr("ID"), tr("Address"), tr("Name"), tr("DB"), tr("Command"), tr("Age"), tr("Idle")},
        clients);
    clientsLayout->addWidget(m_clientsTable, 1);
    connect(clientsRefresh, &QPushButton::clicked, this, &ServerToolsPage::refreshClients);
    m_tabs->addTab(clients, tr("Clients"));

    auto *cluster = new QWidget(m_tabs);
    auto *clusterLayout = new QVBoxLayout(cluster);
    auto *clusterBar = new QHBoxLayout;
    clusterBar->addWidget(new QLabel(tr("Cluster nodes (CLUSTER NODES)"), cluster));
    clusterBar->addStretch();
    auto *clusterRefresh = new QPushButton(tr("Refresh"), cluster);
    clusterBar->addWidget(clusterRefresh);
    clusterLayout->addLayout(clusterBar);
    m_clusterTable = makeTable({tr("ID"), tr("Address"), tr("Flags"), tr("Master"),
                                tr("Slots"), tr("Link"), tr("Memory"), tr("Clients"),
                                tr("Keys")}, cluster);
    m_clusterTable->setObjectName(QStringLiteral("clusterTable"));
    clusterLayout->addWidget(m_clusterTable, 1);
    connect(clusterRefresh, &QPushButton::clicked, this, &ServerToolsPage::refreshCluster);
    m_tabs->addTab(cluster, tr("Cluster"));

    auto *pubsub = new QWidget(m_tabs);
    auto *pubsubLayout = new QVBoxLayout(pubsub);
    auto *channelRow = new QHBoxLayout;
    m_channelEdit = new QLineEdit(pubsub);
    m_channelEdit->setPlaceholderText(tr("Channel or pattern, e.g. events:*"));
    channelRow->addWidget(m_channelEdit, 1);
    auto *subscribeButton = new QPushButton(tr("Subscribe"), pubsub);
    auto *unsubscribeButton = new QPushButton(tr("Unsubscribe"), pubsub);
    auto *publishButton = new QPushButton(tr("Publish"), pubsub);
    channelRow->addWidget(subscribeButton);
    channelRow->addWidget(unsubscribeButton);
    channelRow->addWidget(publishButton);
    pubsubLayout->addLayout(channelRow);
    m_pubsubOutput = new QPlainTextEdit(pubsub);
    m_pubsubOutput->setReadOnly(true);
    pubsubLayout->addWidget(m_pubsubOutput, 1);
    m_tabs->addTab(pubsub, tr("Pub/Sub"));
    connect(subscribeButton, &QPushButton::clicked, this, &ServerToolsPage::subscribeChannel);
    connect(unsubscribeButton, &QPushButton::clicked, this, &ServerToolsPage::unsubscribeChannel);
    connect(publishButton, &QPushButton::clicked, this, &ServerToolsPage::publishMessage);

    auto *monitor = new QWidget(m_tabs);
    auto *monitorLayout = new QVBoxLayout(monitor);
    auto *monitorBar = new QHBoxLayout;
    monitorBar->addWidget(new QLabel(tr("MONITOR stream (separate connection)"), monitor));
    monitorBar->addStretch();
    m_monitorButton = new QPushButton(tr("Start"), monitor);
    monitorBar->addWidget(m_monitorButton);
    monitorLayout->addLayout(monitorBar);
    m_monitorOutput = new QPlainTextEdit(monitor);
    m_monitorOutput->setReadOnly(true);
    monitorLayout->addWidget(m_monitorOutput, 1);
    m_tabs->addTab(monitor, tr("Monitor"));
    connect(m_monitorButton, &QPushButton::clicked, this, [this] {
        if (m_monitor && m_monitor->isReady())
            stopMonitor();
        else
            startMonitor();
    });

    auto *transfer = new QWidget(m_tabs);
    auto *transferLayout = new QVBoxLayout(transfer);
    auto *transferBar = new QHBoxLayout;
    transferBar->addWidget(new QLabel(tr("Pattern"), transfer));
    m_transferPattern = new QLineEdit(QStringLiteral("*"), transfer);
    m_transferPattern->setPlaceholderText(tr("SCAN pattern for export"));
    transferBar->addWidget(m_transferPattern, 1);
    m_exportButton = new QPushButton(tr("Export..."), transfer);
    m_importButton = new QPushButton(tr("Import..."), transfer);
    m_cancelTransferButton = new QPushButton(tr("Cancel"), transfer);
    m_cancelTransferButton->setEnabled(false);
    transferBar->addWidget(m_exportButton);
    transferBar->addWidget(m_importButton);
    transferBar->addWidget(m_cancelTransferButton);
    transferLayout->addLayout(transferBar);
    m_transferStatus = new QLabel(tr("Export uses DUMP/RESTORE JSON Lines; import uses RESTORE REPLACE."), transfer);
    m_transferStatus->setWordWrap(true);
    transferLayout->addWidget(m_transferStatus);
    transferLayout->addStretch();
    m_tabs->addTab(transfer, tr("Transfer"));

    m_transfer = new KeyTransfer(m_connection, this);
    connect(m_exportButton, &QPushButton::clicked, this, &ServerToolsPage::exportKeys);
    connect(m_importButton, &QPushButton::clicked, this, &ServerToolsPage::importKeys);
    connect(m_cancelTransferButton, &QPushButton::clicked, this, &ServerToolsPage::cancelTransfer);
    connect(m_transfer, &KeyTransfer::progress, this,
            [this](qint64 current, qint64 total, const QString &phase) {
        m_transferStatus->setText(total > 0 ? tr("%1 (%2/%3)").arg(phase).arg(current).arg(total)
                                            : tr("%1 (%2)").arg(phase).arg(current));
    });
    connect(m_transfer, &KeyTransfer::finished, this, [this](bool success, const QString &message) {
        m_transferStatus->setText((success ? tr("OK: ") : tr("Error: ")) + message);
        m_exportButton->setEnabled(m_connected);
        m_importButton->setEnabled(m_connected);
        m_cancelTransferButton->setEnabled(false);
    });

    auto *analysis = new QWidget(m_tabs);
    auto *analysisLayout = new QVBoxLayout(analysis);
    auto *analysisBar = new QHBoxLayout;
    analysisBar->addWidget(new QLabel(tr("Pattern"), analysis));
    m_analysisPattern = new QLineEdit(QStringLiteral("*"), analysis);
    m_analysisPattern->setPlaceholderText(tr("SCAN pattern for analysis"));
    analysisBar->addWidget(m_analysisPattern, 1);
    m_analysisType = new QComboBox(analysis);
    m_analysisType->addItem(tr("All types"), QStringLiteral("all"));
    for (const QString &type : {QStringLiteral("string"), QStringLiteral("hash"),
                                QStringLiteral("list"), QStringLiteral("set"),
                                QStringLiteral("zset"), QStringLiteral("stream")}) {
        m_analysisType->addItem(type, type);
    }
    analysisBar->addWidget(m_analysisType);
    m_analyzeButton = new QPushButton(tr("Analyze"), analysis);
    m_cancelAnalysisButton = new QPushButton(tr("Cancel"), analysis);
    m_cancelAnalysisButton->setEnabled(false);
    analysisBar->addWidget(m_analyzeButton);
    analysisBar->addWidget(m_cancelAnalysisButton);
    analysisLayout->addLayout(analysisBar);
    m_analysisStatus = new QLabel(tr("MEMORY USAGE / PTTL scan; results are sorted by size."), analysis);
    m_analysisStatus->setWordWrap(true);
    analysisLayout->addWidget(m_analysisStatus);
    m_analysisTable = makeTable({tr("Key"), tr("Type"), tr("Memory"), tr("TTL")}, analysis);
    m_analysisTable->setObjectName(QStringLiteral("analysisTable"));
    analysisLayout->addWidget(m_analysisTable, 1);
    m_tabs->addTab(analysis, tr("Analysis"));

    m_analyzer = new MemoryAnalyzer(m_connection, this);
    connect(m_analyzeButton, &QPushButton::clicked, this, &ServerToolsPage::analyzeKeys);
    connect(m_cancelAnalysisButton, &QPushButton::clicked, this,
            &ServerToolsPage::cancelAnalysis);
    connect(m_analyzer, &MemoryAnalyzer::progress, this, [this](qint64 scanned) {
        m_analysisStatus->setText(tr("Scanned %1 keys...").arg(scanned));
    });
    connect(m_analyzer, &MemoryAnalyzer::finished, this,
            [this](bool success, const MemoryAnalysisResult &result, const QString &error) {
        m_analyzeButton->setEnabled(m_connected);
        m_cancelAnalysisButton->setEnabled(false);
        if (!success) {
            m_analysisStatus->setText(error);
            return;
        }
        m_analysisStatus->setText(
            tr("%1 keys, %2 total").arg(result.keyCount).arg(formatBytes(result.totalBytes)));
        m_analysisTable->setRowCount(0);
        const qsizetype limit = qMin<qsizetype>(result.keys.size(), 500);
        for (qsizetype index = 0; index < limit; ++index) {
            const KeyMemory &memory = result.keys.at(index);
            const int row = m_analysisTable->rowCount();
            m_analysisTable->insertRow(row);
            m_analysisTable->setItem(row, 0, item(QString::fromUtf8(memory.key)));
            m_analysisTable->setItem(row, 1, item(memory.type));
            m_analysisTable->setItem(row, 2, item(formatBytes(memory.bytes)));
            m_analysisTable->setItem(row, 3, item(formatTtl(memory.ttlMs)));
        }
        m_analysisTable->resizeColumnsToContents();
        m_analysisTable->horizontalHeader()->setStretchLastSection(true);
    });

    layout->addWidget(m_tabs);
}

void ServerToolsPage::setConnected(bool connected)
{
    m_connected = connected;
    ++m_generation;
    if (!connected) {
        if (m_pubsub)
            m_pubsub->disconnectFromHost();
        if (m_monitor)
            m_monitor->disconnectFromHost();
        m_pendingChannels.clear();
        m_slowlogTable->setRowCount(0);
        m_clientsTable->setRowCount(0);
        m_pubsubOutput->clear();
        m_monitorOutput->clear();
        m_monitorButton->setText(tr("Start"));
        m_exportButton->setEnabled(false);
        m_importButton->setEnabled(false);
        m_cancelTransferButton->setEnabled(false);
        m_analyzeButton->setEnabled(false);
        m_cancelAnalysisButton->setEnabled(false);
        if (m_transfer && m_transfer->isRunning())
            m_transfer->cancel();
        if (m_analyzer && m_analyzer->isRunning())
            m_analyzer->cancel();
        return;
    }
    m_exportButton->setEnabled(true);
    m_importButton->setEnabled(true);
    m_analyzeButton->setEnabled(true);
    refreshSlowlog();
    refreshClients();
    refreshCluster();
}

void ServerToolsPage::setConnectionParameters(const QString &host, quint16 port, bool tls,
                                              const QString &username, const QString &password,
                                              int db)
{
    const bool changed = host != m_host || port != m_port || tls != m_tls
        || username != m_username || password != m_password || db != m_db;
    m_host = host;
    m_port = port;
    m_tls = tls;
    m_username = username;
    m_password = password;
    m_db = db;
    if (changed) {
        if (m_pubsub)
            m_pubsub->disconnectFromHost();
        if (m_monitor)
            m_monitor->disconnectFromHost();
        m_monitorButton->setText(tr("Start"));
    }
}

void ServerToolsPage::refreshSlowlog()
{
    if (!m_connected || !m_connection)
        return;
    const quint64 generation = m_generation;
    QPointer<ServerToolsPage> guard(this);
    m_connection->sendCommand({QByteArrayLiteral("SLOWLOG"), QByteArrayLiteral("GET"),
                               QByteArrayLiteral("128")},
                              [guard, generation](const RespValue &reply, const QString &error) {
        if (!guard || guard->m_generation != generation)
            return;
        if (!error.isEmpty() || reply.isError() || reply.type != RespType::Array)
            return;
        guard->m_slowlogTable->setRowCount(0);
        for (const RespValue &entry : reply.elements) {
            if (entry.type != RespType::Array || entry.elements.size() < 4)
                continue;
            const int row = guard->m_slowlogTable->rowCount();
            guard->m_slowlogTable->insertRow(row);
            const RespValue &id = entry.elements[0];
            const RespValue &timestamp = entry.elements[1];
            const RespValue &duration = entry.elements[2];
            const RespValue &args = entry.elements[3];
            guard->m_slowlogTable->setItem(row, 0, item(QString::number(id.integer)));
            guard->m_slowlogTable->setItem(
                row, 1,
                item(QDateTime::fromSecsSinceEpoch(timestamp.integer).toString(
                    QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
            guard->m_slowlogTable->setItem(
                row, 2, item(QStringLiteral("%1 ms").arg(duration.integer / 1000.0, 0, 'f', 2)));
            guard->m_slowlogTable->setItem(row, 3, item(argsText(args)));
        }
        guard->m_slowlogTable->resizeColumnsToContents();
    });
}

void ServerToolsPage::refreshCluster()
{
    if (!m_connected || !m_connection)
        return;
    const quint64 generation = m_generation;
    QPointer<ServerToolsPage> guard(this);
    m_connection->sendCommand({QByteArrayLiteral("CLUSTER"), QByteArrayLiteral("NODES")},
                              [guard, generation](const RespValue &reply, const QString &error) {
        if (!guard || guard->m_generation != generation)
            return;
        if (!error.isEmpty() || reply.isError()) {
            guard->m_clusterTable->setRowCount(0);
            return;
        }
        const QStringList lines = QString::fromUtf8(reply.scalar)
                                      .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        guard->m_clusterTable->setRowCount(0);
        QVector<QPair<int, QString>> masters;
        for (const QString &line : lines) {
            const QStringList fields = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (fields.size() < 8)
                continue;
            const int row = guard->m_clusterTable->rowCount();
            guard->m_clusterTable->insertRow(row);
            guard->m_clusterTable->setItem(row, 0, item(fields.at(0).left(12)));
            guard->m_clusterTable->setItem(row, 1, item(fields.at(1)));
            guard->m_clusterTable->setItem(row, 2, item(fields.at(2)));
            guard->m_clusterTable->setItem(row, 3, item(fields.at(3).left(12)));
            guard->m_clusterTable->setItem(
                row, 4, item(QStringList(fields.mid(8)).join(QLatin1Char(' '))));
            guard->m_clusterTable->setItem(row, 5, item(fields.at(7)));
            guard->m_clusterTable->setItem(row, 6, item(QStringLiteral("-")));
            guard->m_clusterTable->setItem(row, 7, item(QStringLiteral("-")));
            guard->m_clusterTable->setItem(row, 8, item(QStringLiteral("-")));
            const QString flags = fields.at(2);
            if (flags.contains(QLatin1String("master")) && !flags.contains(QLatin1String("fail")))
                masters.append({row, fields.at(1)});
        }
        guard->m_clusterTable->resizeColumnsToContents();
        guard->m_clusterTable->horizontalHeader()->setStretchLastSection(true);

        for (const auto &[row, address] : masters) {
            QString host;
            quint16 port = 0;
            if (!parseNodeAddress(address, &host, &port))
                continue;
            auto *node = new RedisConnection(guard.data());
            QPointer<RedisConnection> nodeGuard(node);
            QObject::connect(node, &RedisClient::stateChanged, guard.data(),
                             [guard, nodeGuard, generation, row](const QString &state) {
                if (!guard || !nodeGuard || guard->m_generation != generation)
                    return;
                if (state != QLatin1String("Ready") && state != QLatin1String("Ready (RESP2)"))
                    return;
                nodeGuard->sendCommand(
                    {QByteArrayLiteral("INFO")},
                    [guard, nodeGuard, generation, row](const RespValue &info,
                                                        const QString &infoError) {
                    if (!guard || !nodeGuard || guard->m_generation != generation)
                        return;
                    if (infoError.isEmpty() && !info.isError()) {
                        QString memory = QStringLiteral("-");
                        QString clients = QStringLiteral("-");
                        QString keys = QStringLiteral("-");
                        const QStringList lines =
                            QString::fromUtf8(info.scalar).split(QLatin1Char('\n'));
                        for (const QString &raw : lines) {
                            const QString line = raw.trimmed();
                            if (line.startsWith(QLatin1String("used_memory_human:")))
                                memory = line.section(QLatin1Char(':'), 1);
                            else if (line.startsWith(QLatin1String("connected_clients:")))
                                clients = line.section(QLatin1Char(':'), 1);
                            else if (line.startsWith(QLatin1String("db0:keys=")))
                                keys = line.section(QLatin1Char(':'), 1)
                                           .section(QLatin1Char(','), 0, 0)
                                           .section(QLatin1Char('='), 1);
                        }
                        if (auto *cell = guard->m_clusterTable->item(row, 6))
                            cell->setText(memory);
                        if (auto *cell = guard->m_clusterTable->item(row, 7))
                            cell->setText(clients);
                        if (auto *cell = guard->m_clusterTable->item(row, 8))
                            cell->setText(keys);
                        guard->m_clusterTable->resizeColumnsToContents();
                    }
                    nodeGuard->disconnectFromHost();
                    nodeGuard->deleteLater();
                });
            });
            QObject::connect(node, &RedisClient::errorOccurred, guard.data(),
                             [nodeGuard](const QString &) {
                if (nodeGuard) {
                    nodeGuard->disconnectFromHost();
                    nodeGuard->deleteLater();
                }
            });
            node->connectToHost(host, port, guard->m_tls, guard->m_username,
                                guard->m_password, 0);
        }
    });
}

void ServerToolsPage::refreshClients()
{
    if (!m_connected || !m_connection)
        return;
    const quint64 generation = m_generation;
    QPointer<ServerToolsPage> guard(this);
    m_connection->sendCommand({QByteArrayLiteral("CLIENT"), QByteArrayLiteral("LIST")},
                              [guard, generation](const RespValue &reply, const QString &error) {
        if (!guard || guard->m_generation != generation)
            return;
        if (!error.isEmpty() || reply.isError())
            return;

        const QString payload = QString::fromUtf8(reply.scalar);
        const QStringList lines = payload.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        guard->m_clientsTable->setRowCount(0);
        for (const QString &line : lines) {
            QHash<QString, QString> fields;
            const QStringList parts = line.trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
            for (const QString &part : parts) {
                const int equals = part.indexOf(QLatin1Char('='));
                if (equals > 0)
                    fields.insert(part.left(equals), part.mid(equals + 1));
            }
            const int row = guard->m_clientsTable->rowCount();
            guard->m_clientsTable->insertRow(row);
            guard->m_clientsTable->setItem(row, 0, item(fields.value(QStringLiteral("id"))));
            guard->m_clientsTable->setItem(row, 1, item(fields.value(QStringLiteral("addr"))));
            guard->m_clientsTable->setItem(row, 2, item(fields.value(QStringLiteral("name"))));
            guard->m_clientsTable->setItem(row, 3, item(fields.value(QStringLiteral("db"))));
            guard->m_clientsTable->setItem(row, 4, item(fields.value(QStringLiteral("cmd"))));
            guard->m_clientsTable->setItem(row, 5, item(fields.value(QStringLiteral("age"))));
            guard->m_clientsTable->setItem(row, 6, item(fields.value(QStringLiteral("idle"))));
        }
        guard->m_clientsTable->resizeColumnsToContents();
        guard->m_clientsTable->horizontalHeader()->setStretchLastSection(true);
    });
}

void ServerToolsPage::subscribeChannel()
{
    if (!m_connected)
        return;
    const QString channel = m_channelEdit->text().trimmed();
    if (channel.isEmpty())
        return;

    if (!m_pubsub) {
        m_pubsub = new RedisConnection(this);
        connect(m_pubsub, &RedisConnection::stateChanged, this, [this](const QString &state) {
            if (state != QLatin1String("Ready") && state != QLatin1String("Ready (RESP2)"))
                return;
            const QStringList pending = m_pendingChannels;
            m_pendingChannels.clear();
            for (const QString &name : pending)
                m_pubsub->sendCommand({QByteArrayLiteral("SUBSCRIBE"), name.toUtf8()});
        });
        connect(m_pubsub, &RedisConnection::replyReady, this, [this](const RespValue &reply) {
            m_pubsubOutput->appendPlainText(reply.toDisplayString());
        });
        connect(m_pubsub, &RedisConnection::pushReceived, this, [this](const RespValue &reply) {
            m_pubsubOutput->appendPlainText(reply.toDisplayString());
        });
        connect(m_pubsub, &RedisConnection::errorOccurred, this, [this](const QString &error) {
            m_pubsubOutput->appendPlainText(QStringLiteral("[error] ") + error);
        });
    }

    if (m_pubsub->isReady()) {
        m_pubsub->sendCommand({QByteArrayLiteral("SUBSCRIBE"), channel.toUtf8()});
    } else {
        m_pendingChannels.append(channel);
        m_pubsub->connectToHost(m_host, m_port, m_tls, m_username, m_password, m_db);
    }
    m_pubsubOutput->appendPlainText(tr("[subscribe] %1").arg(channel));
}

void ServerToolsPage::unsubscribeChannel()
{
    if (!m_pubsub || !m_pubsub->isReady())
        return;
    const QString channel = m_channelEdit->text().trimmed();
    if (channel.isEmpty())
        return;
    m_pubsub->sendCommand({QByteArrayLiteral("UNSUBSCRIBE"), channel.toUtf8()});
    m_pubsubOutput->appendPlainText(tr("[unsubscribe] %1").arg(channel));
}

void ServerToolsPage::publishMessage()
{
    if (!m_connected || !m_connection)
        return;
    const QString channel = m_channelEdit->text().trimmed();
    if (channel.isEmpty())
        return;
    bool ok = false;
    const QString message = QInputDialog::getMultiLineText(this, tr("Publish message"),
                                                            tr("Message:"), {}, &ok);
    if (!ok)
        return;
    m_connection->sendCommand({QByteArrayLiteral("PUBLISH"), channel.toUtf8(),
                               message.toUtf8()},
                              [this, channel](const RespValue &reply, const QString &error) {
        if (!error.isEmpty() || reply.isError())
            m_pubsubOutput->appendPlainText(QStringLiteral("[error] ")
                                            + (error.isEmpty() ? QString::fromUtf8(reply.scalar)
                                                               : error));
        else
            m_pubsubOutput->appendPlainText(
                tr("[publish] %1 -> %2 subscriber(s)").arg(channel).arg(reply.integer));
    });
}

void ServerToolsPage::startMonitor()
{
    if (!m_connected)
        return;
    if (!m_monitor) {
        m_monitor = new RedisConnection(this);
        connect(m_monitor, &RedisConnection::stateChanged, this, [this](const QString &state) {
            if (state == QLatin1String("Ready") || state == QLatin1String("Ready (RESP2)")) {
                m_monitor->sendCommand({QByteArrayLiteral("MONITOR")});
                m_monitorButton->setText(tr("Stop"));
            }
        });
        connect(m_monitor, &RedisConnection::replyReady, this, [this](const RespValue &reply) {
            m_monitorOutput->appendPlainText(reply.toDisplayString());
        });
        connect(m_monitor, &RedisConnection::errorOccurred, this, [this](const QString &error) {
            m_monitorOutput->appendPlainText(QStringLiteral("[error] ") + error);
        });
    }
    m_monitorOutput->clear();
    m_monitor->connectToHost(m_host, m_port, m_tls, m_username, m_password, m_db);
}

void ServerToolsPage::exportKeys()
{
    if (!m_connected || !m_transfer)
        return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export keys"), QStringLiteral("keydis-export.jsonl"),
        tr("JSON Lines (*.jsonl);;All files (*)"));
    if (path.isEmpty())
        return;
    m_exportButton->setEnabled(false);
    m_importButton->setEnabled(false);
    m_cancelTransferButton->setEnabled(true);
    m_transfer->exportToFile(path, m_transferPattern->text().trimmed().toUtf8());
}

void ServerToolsPage::importKeys()
{
    if (!m_connected || !m_transfer)
        return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import keys"), {}, tr("JSON Lines (*.jsonl);;All files (*)"));
    if (path.isEmpty())
        return;
    if (QMessageBox::question(
            this, tr("Import keys"),
            tr("Import uses RESTORE REPLACE and may overwrite existing keys. Continue?"))
        != QMessageBox::Yes) {
        return;
    }
    m_exportButton->setEnabled(false);
    m_importButton->setEnabled(false);
    m_cancelTransferButton->setEnabled(true);
    m_transfer->importFromFile(path);
}

void ServerToolsPage::cancelTransfer()
{
    if (m_transfer)
        m_transfer->cancel();
}

void ServerToolsPage::analyzeKeys()
{
    if (!m_connected || !m_analyzer || m_analyzer->isRunning())
        return;
    m_analysisTable->setRowCount(0);
    m_analysisStatus->setText(tr("Analyzing memory..."));
    m_analyzeButton->setEnabled(false);
    m_cancelAnalysisButton->setEnabled(true);
    m_analyzer->start(m_analysisPattern->text().trimmed().toUtf8(),
                      m_analysisType->currentData().toString());
}

void ServerToolsPage::cancelAnalysis()
{
    if (m_analyzer)
        m_analyzer->cancel();
}

void ServerToolsPage::stopMonitor()
{
    if (m_monitor)
        m_monitor->disconnectFromHost();
    m_monitorButton->setText(tr("Start"));
}

} // namespace KeyDis
