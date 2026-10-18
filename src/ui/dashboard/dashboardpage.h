// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QHash>
#include <QPointer>
#include <QWidget>

class QCheckBox;
class QLabel;
class QTableWidget;
class QTimer;

namespace KeyDis {

class RedisConnection;
struct RedisInfo;

class DashboardPage : public QWidget
{
    Q_OBJECT

public:
    explicit DashboardPage(RedisConnection *connection, QWidget *parent = nullptr);

    void setConnected(bool connected);
    void refresh();

signals:
    void databaseActivated(int db);

private:
    void updateUi(const RedisInfo &info);
    void setStatus(const QString &status);

    QPointer<RedisConnection> m_connection;
    QHash<QString, QLabel *> m_values;
    QLabel *m_updatedLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QCheckBox *m_autoRefresh = nullptr;
    QTableWidget *m_dbTable = nullptr;
    QTimer *m_timer = nullptr;
    bool m_connected = false;
    quint64 m_generation = 0;
};

} // namespace KeyDis
