// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include "core/conn/connectionprofile.h"
#include "core/keyspace/keyinfo.h"
#include "core/net/redisrouter.h"

#include <KXmlGuiWindow>

class QAction;
class QCheckBox;
class QComboBox;
class QDockWidget;
class QLabel;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
class QModelIndex;
class QPlainTextEdit;
class QPoint;
class QPushButton;
class QSpinBox;
class QTableView;
class QStackedWidget;
class QTabWidget;
class QTreeView;
class QTemporaryFile;

namespace KeyDis {

class DashboardPage;
class KeyScanner;
class KeyTableModel;
class NamespaceTreeModel;
class ServerToolsPage;
class SentinelWatcher;
class SshTunnel;
class ValuePanel;

class MainWindow : public KXmlGuiWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void setConnectionParameters(const QString &host, quint16 port, bool tls,
                                 const QString &username, const QString &password, int db);
    void setUnixSocketPath(const QString &path);
    void connectNow();
    void showTab(const QString &name);
    void setConnectionsDockVisible(bool visible);
    [[nodiscard]] bool connectionsDockVisible() const;
    [[nodiscard]] QAction *connectionsToggleAction() const;
    [[nodiscard]] bool isReadOnly() const;

public slots:
    void newWindow();
    void connectProfileById(const QString &profileId);
    void openActiveProfileInNewWindow();
    void deleteSelectedKeys();
    void setSelectedKeysTtl();
    void setClusterMode(bool cluster);
    void toggleConnection();
    void refreshKeys();
    void renameSelectedKey();
    void setReadOnlyMode(bool readOnly);

signals:
    void sessionTitleChanged(const QString &title);
    void newSessionRequested();
    void newSessionWithProfileRequested(const QString &profileId);
    void connectionsDockVisibilityChanged(bool visible);
    void readOnlyChanged(bool readOnly);

private:
    void buildUi();
    void createContextActions();
    void connectOrDisconnect(bool retryAuth = false);
    void startScan();
    void selectDatabase(int db);
    void loadMore();
    void selectKey(const QModelIndex &current, const QModelIndex &previous);
    void selectTreeKey(const QModelIndex &current, const QModelIndex &previous);
    void showKeyContextMenu(const QPoint &position);
    [[nodiscard]] QVector<KeyInfo> selectedKeys() const;
    void showTreeContextMenu(const QPoint &position);
    void renameKey(const KeyInfo &info);
    void deleteKey(const KeyInfo &info);
    void copyKey(const KeyInfo &info);
    void copyValue(const KeyInfo &info);
    void removeKeyFromViews(const QByteArray &key);
    void applyNamespaceFilter(const QString &path);
    void copyKeyToProfile(const KeyInfo &info);
    void loadProfiles();
    void rebuildProfileList();
    void applyProfile(const ConnectionProfile &profile);
    void createProfile();
    void editProfile();
    void deleteProfile();
    void duplicateProfile();
    void connectProfile();
    void updateProfileButtons();
    void openProfileInNewWindow(const QString &profileId);
    void saveProfiles();
    [[nodiscard]] ConnectionProfile *selectedProfile();
    void syncDatabaseUi(int db);
    void sendCommand();
    void appendOutput(const QString &text);
    void setConnectedUi(bool connected);

    RedisRouter m_router;
    KeyScanner *m_scanner = nullptr;
    KeyTableModel *m_model = nullptr;
    ValuePanel *m_valuePanel = nullptr;
    DashboardPage *m_dashboard = nullptr;
    ServerToolsPage *m_tools = nullptr;
    SshTunnel *m_sshTunnel = nullptr;
    SentinelWatcher *m_sentinelWatcher = nullptr;
    QString m_effectiveHost;
    QString m_ephemeralUnixSocket;
    quint16 m_effectivePort = 0;
    QTabWidget *m_tabs = nullptr;
    QTreeWidget *m_profileList = nullptr;
    QDockWidget *m_connectionsDock = nullptr;
    KeyInfo m_contextKey;
    QString m_contextPath;
    QAction *m_keyRenameAction = nullptr;
    QAction *m_keyDeleteAction = nullptr;
    QAction *m_keyTtlAction = nullptr;
    QAction *m_keyCopyKeyAction = nullptr;
    QAction *m_keyCopyValueAction = nullptr;
    QAction *m_keyCopyToAction = nullptr;
    QAction *m_keyRefreshAction = nullptr;
    QAction *m_namespaceFilterAction = nullptr;
    QAction *m_copyPrefixAction = nullptr;
    QSet<QString> m_collapsedGroups;
    QVector<ConnectionProfile> m_profiles;
    QString m_activeProfileId;
    QTemporaryFile *m_uiFile = nullptr;

    QLineEdit *m_hostEdit = nullptr;
    QSpinBox *m_portSpin = nullptr;
    QCheckBox *m_tlsCheck = nullptr;
    QCheckBox *m_readOnlyCheck = nullptr;
    QCheckBox *m_clusterCheck = nullptr;
    QCheckBox *m_readOnlyDock = nullptr;
    QPushButton *m_profileConnectButton = nullptr;
    QPushButton *m_profileNewWindowButton = nullptr;
    QPushButton *m_retryButton = nullptr;
    QPushButton *m_profileEditButton = nullptr;
    QPushButton *m_profileDeleteButton = nullptr;
    QPushButton *m_profileDuplicateButton = nullptr;
    QLabel *m_profileEmptyLabel = nullptr;
    QLineEdit *m_usernameEdit = nullptr;
    QLineEdit *m_passwordEdit = nullptr;
    QSpinBox *m_dbSpin = nullptr;
    QPushButton *m_connectButton = nullptr;
    QLabel *m_statusLabel = nullptr;

    QLineEdit *m_filterEdit = nullptr;
    QComboBox *m_typeCombo = nullptr;
    QComboBox *m_dbCombo = nullptr;
    QComboBox *m_viewMode = nullptr;
    QPushButton *m_scanButton = nullptr;
    QPushButton *m_loadMoreButton = nullptr;
    QPushButton *m_indexAllButton = nullptr;
    QLabel *m_scanStatus = nullptr;
    bool m_indexingAll = false;
    bool m_authPromptTried = false;
    quint64 m_keyGeneration = 0;
    QTableView *m_keyTable = nullptr;
    QTreeView *m_treeView = nullptr;
    NamespaceTreeModel *m_treeModel = nullptr;
    QStackedWidget *m_keyViews = nullptr;

    QPlainTextEdit *m_output = nullptr;
    QLineEdit *m_commandEdit = nullptr;
    QPushButton *m_sendButton = nullptr;
};

} // namespace KeyDis
