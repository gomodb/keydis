// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QPointer>
#include <QStringList>
#include <QWidget>

class QLineEdit;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTabWidget;

namespace KeyDis {

class KeyTransfer;
class MemoryAnalyzer;
class RedisConnection;

class ServerToolsPage : public QWidget
{
    Q_OBJECT

public:
    explicit ServerToolsPage(RedisConnection *connection, QWidget *parent = nullptr);

    void setConnected(bool connected);
    void setConnectionParameters(const QString &host, quint16 port, bool tls,
                                 const QString &username, const QString &password, int db);

public slots:
    void refreshSlowlog();
    void refreshClients();
    void subscribeChannel();
    void unsubscribeChannel();
    void publishMessage();
    void startMonitor();
    void stopMonitor();
    void exportKeys();
    void importKeys();
    void cancelTransfer();
    void analyzeKeys();
    void cancelAnalysis();

private:
    QPointer<RedisConnection> m_connection;
    QTabWidget *m_tabs = nullptr;
    QTableWidget *m_slowlogTable = nullptr;
    QTableWidget *m_clientsTable = nullptr;
    QLineEdit *m_channelEdit = nullptr;
    QPlainTextEdit *m_pubsubOutput = nullptr;
    QPlainTextEdit *m_monitorOutput = nullptr;
    QPushButton *m_monitorButton = nullptr;
    QLineEdit *m_transferPattern = nullptr;
    QLabel *m_transferStatus = nullptr;
    QPushButton *m_exportButton = nullptr;
    QPushButton *m_importButton = nullptr;
    QPushButton *m_cancelTransferButton = nullptr;
    KeyTransfer *m_transfer = nullptr;
    QLineEdit *m_analysisPattern = nullptr;
    QComboBox *m_analysisType = nullptr;
    QLabel *m_analysisStatus = nullptr;
    QTableWidget *m_analysisTable = nullptr;
    QPushButton *m_analyzeButton = nullptr;
    QPushButton *m_cancelAnalysisButton = nullptr;
    MemoryAnalyzer *m_analyzer = nullptr;
    RedisConnection *m_pubsub = nullptr;
    RedisConnection *m_monitor = nullptr;
    QString m_host;
    quint16 m_port = 6379;
    bool m_tls = false;
    QString m_username;
    QString m_password;
    int m_db = 0;
    QStringList m_pendingChannels;
    bool m_connected = false;
    quint64 m_generation = 0;
};

} // namespace KeyDis
