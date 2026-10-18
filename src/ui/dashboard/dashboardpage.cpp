// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "ui/dashboard/dashboardpage.h"

#include "core/insights/redisinfo.h"
#include "core/keyspace/keyinfo.h"
#include "core/net/redisconnection.h"

#include <KColorScheme>

#include <QCheckBox>
#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace KeyDis {

namespace {

QLabel *makeSectionTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    QFont font = label->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() + 1.0);
    label->setFont(font);
    return label;
}

QTableWidgetItem *readOnlyItem(const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

} // namespace

DashboardPage::DashboardPage(RedisConnection *connection, QWidget *parent)
    : QWidget(parent)
    , m_connection(connection)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    auto *header = new QHBoxLayout;
    header->addWidget(makeSectionTitle(tr("Overview"), this));
    header->addStretch();

    m_updatedLabel = new QLabel(tr("Not connected"), this);
    m_updatedLabel->setForegroundRole(QPalette::PlaceholderText);
    header->addWidget(m_updatedLabel);

    m_autoRefresh = new QCheckBox(tr("Auto refresh"), this);
    m_autoRefresh->setChecked(true);
    header->addWidget(m_autoRefresh);

    auto *refreshButton = new QPushButton(tr("Refresh"), this);
    header->addWidget(refreshButton);
    layout->addLayout(header);

    auto *cards = new QHBoxLayout;
    cards->setSpacing(12);

    auto makeCard = [this, &cards](const QString &title,
                                   const QList<QPair<QString, QString>> &rows) {
        auto *card = new QFrame(this);
        card->setObjectName(QStringLiteral("infoCard"));
        card->setFrameShape(QFrame::StyledPanel);
        card->setAutoFillBackground(true);
        card->setBackgroundRole(QPalette::AlternateBase);
        card->setMinimumWidth(220);
        const KColorScheme scheme(QPalette::Active, KColorScheme::View);
        const QString background =
            scheme.background(KColorScheme::AlternateBackground).color().name();
        const QString border =
            scheme.foreground(KColorScheme::InactiveText).color().name();
        card->setStyleSheet(QStringLiteral("QFrame#infoCard {"
                                           " background-color: %1;"
                                           " border: 1px solid %2;"
                                           " border-radius: 8px;"
                                           "}")
                                .arg(background, border));

        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(14, 12, 14, 12);
        cardLayout->addWidget(makeSectionTitle(title, card));
        cardLayout->addSpacing(6);

        auto *grid = new QGridLayout;
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(8);
        int row = 0;
        for (const auto &[field, label] : rows) {
            auto *keyLabel = new QLabel(label, card);
            keyLabel->setForegroundRole(QPalette::PlaceholderText);
            keyLabel->setWordWrap(true);
            grid->addWidget(keyLabel, row, 0, Qt::AlignLeft | Qt::AlignVCenter);

            auto *valueLabel = new QLabel(QStringLiteral("-"), card);
            valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
            valueLabel->setWordWrap(true);
            QFont valueFont = valueLabel->font();
            valueFont.setBold(true);
            valueLabel->setFont(valueFont);
            grid->addWidget(valueLabel, row, 1, Qt::AlignLeft | Qt::AlignVCenter);
            m_values.insert(field, valueLabel);
            ++row;
        }
        grid->setColumnStretch(1, 1);
        cardLayout->addLayout(grid);
        cardLayout->addStretch();
        cards->addWidget(card, 1);
    };

    makeCard(tr("Server"),
             {{QStringLiteral("redis_version"), tr("Redis version")},
              {QStringLiteral("os"), tr("OS")},
              {QStringLiteral("process_id"), tr("Process ID")}});
    makeCard(tr("Memory"),
             {{QStringLiteral("used_memory_human"), tr("Used memory")},
              {QStringLiteral("used_memory_peak_human"), tr("Peak memory")},
              {QStringLiteral("used_memory_lua_human"), tr("Lua memory")}});
    makeCard(tr("Stats"),
             {{QStringLiteral("connected_clients"), tr("Connected clients")},
              {QStringLiteral("total_connections_received"), tr("Total connections")},
              {QStringLiteral("total_commands_processed"), tr("Total commands")}});
    layout->addLayout(cards);

    auto *tableHeader = new QHBoxLayout;
    tableHeader->addWidget(makeSectionTitle(tr("Key Statistics"), this));
    tableHeader->addStretch();
    m_statusLabel = new QLabel(this);
    m_statusLabel->setForegroundRole(QPalette::PlaceholderText);
    tableHeader->addWidget(m_statusLabel);
    layout->addLayout(tableHeader);

    m_dbTable = new QTableWidget(0, 4, this);
    m_dbTable->setHorizontalHeaderLabels({tr("DB"), tr("Keys"), tr("Expires"), tr("Avg TTL")});
    m_dbTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_dbTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_dbTable->setAlternatingRowColors(true);
    m_dbTable->verticalHeader()->setVisible(false);
    m_dbTable->horizontalHeader()->setStretchLastSection(true);
    m_dbTable->setColumnWidth(0, 90);
    m_dbTable->setColumnWidth(1, 120);
    m_dbTable->setColumnWidth(2, 120);
    layout->addWidget(m_dbTable, 1);

    connect(m_dbTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        QTableWidgetItem *item = m_dbTable->item(row, 0);
        if (!item)
            return;
        bool ok = false;
        const int db = item->text().mid(2).toInt(&ok);
        if (ok)
            emit databaseActivated(db);
    });

    m_timer = new QTimer(this);
    m_timer->setInterval(5000);
    connect(m_timer, &QTimer::timeout, this, &DashboardPage::refresh);
    connect(refreshButton, &QPushButton::clicked, this, &DashboardPage::refresh);
    connect(m_autoRefresh, &QCheckBox::toggled, this, [this](bool enabled) {
        if (enabled && m_connected)
            m_timer->start();
        else
            m_timer->stop();
    });
}

void DashboardPage::setConnected(bool connected)
{
    if (m_connected == connected && connected)
        return;
    m_connected = connected;
    ++m_generation;
    if (!connected) {
        m_timer->stop();
        m_updatedLabel->setText(tr("Not connected"));
        m_statusLabel->clear();
        m_dbTable->setRowCount(0);
        for (QLabel *label : std::as_const(m_values))
            label->setText(QStringLiteral("-"));
        return;
    }
    if (m_autoRefresh->isChecked())
        m_timer->start();
    refresh();
}

void DashboardPage::refresh()
{
    if (!m_connected || !m_connection)
        return;
    m_statusLabel->setText(tr("Refreshing..."));
    const quint64 generation = m_generation;
    QPointer<DashboardPage> guard(this);
    m_connection->sendCommand({QByteArrayLiteral("INFO")},
                              [guard, generation](const RespValue &reply, const QString &error) {
        if (!guard || guard->m_generation != generation)
            return;
        if (!error.isEmpty()) {
            guard->setStatus(error);
            return;
        }
        if (reply.isError()) {
            guard->setStatus(QString::fromUtf8(reply.scalar));
            return;
        }
        guard->updateUi(RedisInfo::parse(reply.scalar));
    });
}

void DashboardPage::updateUi(const RedisInfo &info)
{
    auto setValue = [this, &info](const QString &field) {
        if (QLabel *label = m_values.value(field))
            label->setText(info.value(field));
    };
    setValue(QStringLiteral("redis_version"));
    setValue(QStringLiteral("os"));
    setValue(QStringLiteral("process_id"));
    setValue(QStringLiteral("used_memory_human"));
    setValue(QStringLiteral("used_memory_peak_human"));
    setValue(QStringLiteral("used_memory_lua_human"));
    setValue(QStringLiteral("connected_clients"));
    setValue(QStringLiteral("total_connections_received"));
    setValue(QStringLiteral("total_commands_processed"));

    QVector<KeyspaceInfo> databases = info.databases;
    std::sort(databases.begin(), databases.end(),
              [](const KeyspaceInfo &left, const KeyspaceInfo &right) {
                  return left.db < right.db;
              });

    m_dbTable->setRowCount(0);
    for (const KeyspaceInfo &keyspace : std::as_const(databases)) {
        const int row = m_dbTable->rowCount();
        m_dbTable->insertRow(row);
        m_dbTable->setItem(row, 0, readOnlyItem(QStringLiteral("db%1").arg(keyspace.db)));
        m_dbTable->setItem(row, 1, readOnlyItem(QString::number(keyspace.keys)));
        m_dbTable->setItem(row, 2, readOnlyItem(QString::number(keyspace.expires)));
        m_dbTable->setItem(row, 3,
                           readOnlyItem(keyspace.avgTtlMs > 0 ? formatTtl(keyspace.avgTtlMs)
                                                             : QStringLiteral("-")));
    }

    m_updatedLabel->setText(
        tr("Updated %1").arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))));
    m_statusLabel->clear();
}

void DashboardPage::setStatus(const QString &status)
{
    m_statusLabel->setText(status);
}

} // namespace KeyDis
