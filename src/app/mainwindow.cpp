// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "app/mainwindow.h"

#include "core/conn/profilestore.h"
#include "core/conn/sentinelresolver.h"
#include "core/conn/sentinelwatcher.h"
#include "core/net/clusterclient.h"
#include "core/net/redisconnection.h"
#include "core/net/sshtunnel.h"
#include "core/security/secretstore.h"
#include "ui/dialogs/profiledialog.h"
#include "core/keyspace/keyscanner.h"
#include "core/safety/commandsafety.h"
#include "core/resp/commandencoder.h"
#include "ui/dashboard/dashboardpage.h"
#include "ui/tools/servertools.h"
#include "ui/models/keytablemodel.h"
#include "ui/models/namespacetreemodel.h"
#include "ui/values/valuepanel.h"

#include <QApplication>
#include <QCheckBox>
#include <QDockWidget>
#include <QClipboard>
#include <QMenu>
#include <QMenuBar>
#include <QKeySequence>

#include <algorithm>
#include <functional>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QIcon>
#include <QPixmap>
#include <QGroupBox>
#include <QGuiApplication>
#include <QNetworkProxy>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QTreeWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTreeView>
#include <QSpinBox>
#include <QStandardPaths>
#include <QSplitter>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryFile>
#include <QUuid>
#include <QVBoxLayout>

namespace KeyDis {

namespace {

TlsConfig tlsConfigForProfile(const ConnectionProfile &profile)
{
    TlsConfig config;
    config.enabled = profile.tls;
    config.caFile = profile.tlsCaFile;
    config.certFile = profile.tlsCertFile;
    config.keyFile = profile.tlsKeyFile;
    config.verifyPeer = profile.tlsVerifyPeer;
    return config;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : KXmlGuiWindow(parent)
{
    setWindowTitle(QStringLiteral("KeyDis"));
    resize(1200, 800);
    m_router.setBackend(std::make_unique<RedisConnection>());
    buildUi();

    loadProfiles();

    QString uiFile = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                            QStringLiteral("keydis/keydisui.rc"));
    if (uiFile.isEmpty()) {
        const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dataDir);
        const QString candidate = dataDir + QStringLiteral("/keydisui.rc");
        if (!QFile::exists(candidate))
            QFile::copy(QStringLiteral(":/keydis/keydisui.rc"), candidate);
        if (QFile::exists(candidate))
            uiFile = candidate;
    }
    if (uiFile.isEmpty()) {
        m_uiFile = new QTemporaryFile(QDir::tempPath() + QStringLiteral("/keydisui-XXXXXX.rc"), this);
        if (m_uiFile->open()) {
            QFile resource(QStringLiteral(":/keydis/keydisui.rc"));
            if (resource.open(QIODevice::ReadOnly))
                m_uiFile->write(resource.readAll());
            m_uiFile->close();
            uiFile = m_uiFile->fileName();
        }
    }
    setupGUI(Default, uiFile);

    if (QAction *dockToggle = connectionsToggleAction()) {
        auto *viewMenu = menuBar()->addMenu(tr("&View"));
        viewMenu->addAction(dockToggle);
    }
    createContextActions();
}

void MainWindow::createContextActions()
{
    const auto makeAction = [this](const QString &name, const QString &text) {
        auto *action = new QAction(text, this);
        action->setObjectName(name);
        return action;
    };
    m_keyRenameAction = makeAction(QStringLiteral("contextRenameKeyAction"), tr("Rename..."));
    connect(m_keyRenameAction, &QAction::triggered, this, [this] {
        if (m_contextKey.isValid())
            renameKey(m_contextKey);
        else
            renameSelectedKey();
    });
    m_keyDeleteAction = makeAction(QStringLiteral("contextDeleteKeyAction"), tr("Delete..."));
    connect(m_keyDeleteAction, &QAction::triggered, this, [this] {
        if (m_contextKey.isValid())
            deleteKey(m_contextKey);
        else
            deleteSelectedKeys();
    });
    m_keyTtlAction = makeAction(QStringLiteral("contextTtlAction"), tr("Set TTL..."));
    connect(m_keyTtlAction, &QAction::triggered, this, &MainWindow::setSelectedKeysTtl);
    m_keyCopyKeyAction = makeAction(QStringLiteral("contextCopyKeyAction"), tr("Copy key"));
    connect(m_keyCopyKeyAction, &QAction::triggered, this, [this] {
        if (m_contextKey.isValid())
            copyKey(m_contextKey);
    });
    m_keyCopyValueAction =
        makeAction(QStringLiteral("contextCopyValueAction"), tr("Copy value"));
    connect(m_keyCopyValueAction, &QAction::triggered, this, [this] {
        if (m_contextKey.isValid())
            copyValue(m_contextKey);
    });
    m_keyCopyToAction =
        makeAction(QStringLiteral("contextCopyToAction"), tr("Copy to connection..."));
    connect(m_keyCopyToAction, &QAction::triggered, this, [this] {
        if (m_contextKey.isValid())
            copyKeyToProfile(m_contextKey);
    });
    m_keyRefreshAction =
        makeAction(QStringLiteral("contextRefreshValueAction"), tr("Refresh value"));
    connect(m_keyRefreshAction, &QAction::triggered, this, [this] {
        if (m_contextKey.isValid())
            m_valuePanel->loadKey(m_contextKey);
    });
    m_namespaceFilterAction =
        makeAction(QStringLiteral("contextFilterNamespaceAction"), tr("Filter this namespace"));
    connect(m_namespaceFilterAction, &QAction::triggered, this, [this] {
        if (!m_contextPath.isEmpty())
            applyNamespaceFilter(m_contextPath);
    });
    m_copyPrefixAction = makeAction(QStringLiteral("contextCopyPrefixAction"), tr("Copy prefix"));
    connect(m_copyPrefixAction, &QAction::triggered, this, [this] {
        if (!m_contextPath.isEmpty() && m_treeModel)
            QGuiApplication::clipboard()->setText(m_contextPath + m_treeModel->separator());
    });
}

QAction *MainWindow::connectionsToggleAction() const
{
    return m_connectionsDock ? m_connectionsDock->toggleViewAction() : nullptr;
}

bool MainWindow::isReadOnly() const
{
    return m_readOnlyDock && m_readOnlyDock->isChecked();
}

void MainWindow::setReadOnlyMode(bool readOnly)
{
    if (m_readOnlyDock)
        m_readOnlyDock->setChecked(readOnly);
}

void MainWindow::toggleConnection()
{
    connectOrDisconnect();
}

void MainWindow::refreshKeys()
{
    if (!m_router.isReady()) {
        if (m_scanStatus)
            m_scanStatus->setText(tr("Not connected"));
        return;
    }
    startScan();
}

void MainWindow::renameSelectedKey()
{
    const QVector<KeyInfo> keys = selectedKeys();
    if (keys.size() == 1)
        renameKey(keys.first());
}

MainWindow::~MainWindow()
{
    if (m_connectionsDock)
        disconnect(m_connectionsDock, &QDockWidget::visibilityChanged, this, nullptr);
    if (m_sshTunnel)
        m_sshTunnel->stop();
    if (m_sentinelWatcher)
        m_sentinelWatcher->stop();
}

void MainWindow::setConnectionParameters(const QString &host, quint16 port, bool tls,
                                         const QString &username, const QString &password, int db)
{
    m_hostEdit->setText(host);
    m_portSpin->setValue(port);
    m_tlsCheck->setChecked(tls);
    m_usernameEdit->setText(username);
    m_passwordEdit->setText(password);
    m_dbSpin->setValue(db);
}

void MainWindow::setUnixSocketPath(const QString &path)
{
    m_ephemeralUnixSocket = path;
}

void MainWindow::connectNow()
{
    if (!m_router.isReady())
        connectOrDisconnect();
}

void MainWindow::setClusterMode(bool cluster)
{
    m_clusterCheck->setChecked(cluster);
    m_dbSpin->setEnabled(!cluster);
    m_dbCombo->setEnabled(!cluster);
}

void MainWindow::showTab(const QString &name)
{
    if (name == QLatin1String("browser"))
        m_tabs->setCurrentIndex(1);
    else if (name == QLatin1String("cli"))
        m_tabs->setCurrentIndex(2);
    else if (name == QLatin1String("tools"))
        m_tabs->setCurrentIndex(3);
    else
        m_tabs->setCurrentIndex(0);
}

void MainWindow::setConnectionsDockVisible(bool visible)
{
    if (!m_connectionsDock)
        return;
    if (visible) {
        m_connectionsDock->show();
        m_connectionsDock->raise();
    } else {
        m_connectionsDock->hide();
    }
}

bool MainWindow::connectionsDockVisible() const
{
    return m_connectionsDock && !m_connectionsDock->isHidden();
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);

    auto *connectionBox = new QGroupBox(tr("Connection"), central);
    auto *connectionLayout = new QHBoxLayout(connectionBox);

    m_hostEdit = new QLineEdit(QStringLiteral("127.0.0.1"), connectionBox);
    m_hostEdit->setObjectName(QStringLiteral("hostEdit"));
    m_hostEdit->setPlaceholderText(tr("Host"));
    connectionLayout->addWidget(m_hostEdit, 2);

    m_portSpin = new QSpinBox(connectionBox);
    m_portSpin->setObjectName(QStringLiteral("portSpin"));
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(6379);
    connectionLayout->addWidget(m_portSpin);

    m_tlsCheck = new QCheckBox(tr("TLS"), connectionBox);
    m_tlsCheck->setObjectName(QStringLiteral("tlsCheck"));
    connectionLayout->addWidget(m_tlsCheck);

    m_readOnlyCheck = new QCheckBox(tr("Read-only"), connectionBox);
    m_readOnlyCheck->setObjectName(QStringLiteral("readOnlyCheck"));
    m_readOnlyCheck->setToolTip(tr("Disable editing and CLI write access for this session"));
    connectionLayout->addWidget(m_readOnlyCheck);
    m_clusterCheck = new QCheckBox(tr("Cluster"), connectionBox);
    m_clusterCheck->setObjectName(QStringLiteral("clusterCheck"));
    m_clusterCheck->setToolTip(tr("Connect through Redis Cluster slot routing"));
    connectionLayout->addWidget(m_clusterCheck);

    m_usernameEdit = new QLineEdit(connectionBox);
    m_usernameEdit->setObjectName(QStringLiteral("usernameEdit"));
    m_usernameEdit->setPlaceholderText(tr("ACL user (optional)"));
    connectionLayout->addWidget(m_usernameEdit, 1);

    m_passwordEdit = new QLineEdit(connectionBox);
    m_passwordEdit->setObjectName(QStringLiteral("passwordEdit"));
    m_passwordEdit->setPlaceholderText(tr("Password (optional)"));
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    connectionLayout->addWidget(m_passwordEdit, 1);

    m_dbSpin = new QSpinBox(connectionBox);
    m_dbSpin->setObjectName(QStringLiteral("dbSpin"));
    m_dbSpin->setRange(0, 15);
    m_dbSpin->setPrefix(tr("DB "));
    connectionLayout->addWidget(m_dbSpin);

    m_connectButton = new QPushButton(tr("Connect"), connectionBox);
    m_connectButton->setObjectName(QStringLiteral("connectButton"));
    connectionLayout->addWidget(m_connectButton);

    layout->addWidget(connectionBox);
    connectionBox->hide();

    auto *statusRow = new QHBoxLayout;
    m_statusLabel = new QLabel(tr("Disconnected"), central);
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusRow->addWidget(m_statusLabel, 1);
    m_retryButton = new QPushButton(tr("Retry"), central);
    m_retryButton->setObjectName(QStringLiteral("retryButton"));
    m_retryButton->hide();
    statusRow->addWidget(m_retryButton);
    layout->addLayout(statusRow);
    connect(m_retryButton, &QPushButton::clicked, this, [this] { connectOrDisconnect(); });

    m_tabs = new QTabWidget(central);
    m_tabs->setObjectName(QStringLiteral("tabs"));

    m_dashboard = new DashboardPage(&m_router, m_tabs);
    m_tabs->addTab(m_dashboard, tr("Dashboard"));

    auto *browser = new QWidget(m_tabs);
    auto *browserLayout = new QVBoxLayout(browser);

    auto *filterRow = new QHBoxLayout;

    m_dbCombo = new QComboBox(browser);
    m_dbCombo->setObjectName(QStringLiteral("dbCombo"));
    for (int db = 0; db < 16; ++db)
        m_dbCombo->addItem(tr("DB %1").arg(db), db);
    filterRow->addWidget(m_dbCombo);

    m_filterEdit = new QLineEdit(browser);
    m_filterEdit->setObjectName(QStringLiteral("filterEdit"));
    m_filterEdit->setPlaceholderText(tr("SCAN pattern, e.g. user:* or *session*"));
    filterRow->addWidget(m_filterEdit, 1);

    m_typeCombo = new QComboBox(browser);
    m_typeCombo->setObjectName(QStringLiteral("typeCombo"));
    m_typeCombo->addItem(tr("All types"), QStringLiteral("all"));
    m_typeCombo->addItem(QStringLiteral("string"), QStringLiteral("string"));
    m_typeCombo->addItem(QStringLiteral("hash"), QStringLiteral("hash"));
    m_typeCombo->addItem(QStringLiteral("list"), QStringLiteral("list"));
    m_typeCombo->addItem(QStringLiteral("set"), QStringLiteral("set"));
    m_typeCombo->addItem(QStringLiteral("zset"), QStringLiteral("zset"));
    m_typeCombo->addItem(QStringLiteral("stream"), QStringLiteral("stream"));
    filterRow->addWidget(m_typeCombo);

    m_viewMode = new QComboBox(browser);
    m_viewMode->setObjectName(QStringLiteral("viewMode"));
    m_viewMode->addItem(tr("Tree"), 0);
    m_viewMode->addItem(tr("List"), 1);
    filterRow->addWidget(m_viewMode);

    m_scanButton = new QPushButton(tr("Scan"), browser);
    m_scanButton->setObjectName(QStringLiteral("scanButton"));
    m_scanButton->setEnabled(false);
    filterRow->addWidget(m_scanButton);

    m_loadMoreButton = new QPushButton(tr("Load more"), browser);
    m_loadMoreButton->setObjectName(QStringLiteral("loadMoreButton"));
    m_loadMoreButton->setEnabled(false);
    filterRow->addWidget(m_loadMoreButton);

    m_indexAllButton = new QPushButton(tr("Index all"), browser);
    m_indexAllButton->setObjectName(QStringLiteral("indexAllButton"));
    m_indexAllButton->setToolTip(tr("Scan the entire DB to make namespace counts exact"));
    m_indexAllButton->setEnabled(false);
    filterRow->addWidget(m_indexAllButton);
    browserLayout->addLayout(filterRow);

    m_scanStatus = new QLabel(tr("Not connected"), browser);
    m_scanStatus->setObjectName(QStringLiteral("scanStatus"));
    browserLayout->addWidget(m_scanStatus);

    auto *splitter = new QSplitter(Qt::Horizontal, browser);
    splitter->setHandleWidth(8);
    splitter->setOpaqueResize(true);
    auto *keyPane = new QWidget(splitter);
    keyPane->setMinimumWidth(380);
    auto *keyLayout = new QVBoxLayout(keyPane);
    keyLayout->setContentsMargins(0, 0, 0, 0);

    m_keyViews = new QStackedWidget(keyPane);
    m_keyViews->setObjectName(QStringLiteral("keyViews"));

    m_keyTable = new QTableView(m_keyViews);
    m_keyTable->setObjectName(QStringLiteral("keyTable"));
    m_model = new KeyTableModel(this);
    m_keyTable->setModel(m_model);
    m_keyTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_keyTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_keyTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_keyTable->setAlternatingRowColors(true);
    m_keyTable->setWordWrap(false);
    m_keyTable->horizontalHeader()->setMinimumSectionSize(64);
    m_keyTable->horizontalHeader()->setSectionResizeMode(KeyTableModel::KeyColumn,
                                                        QHeaderView::Stretch);
    m_keyTable->horizontalHeader()->setSectionResizeMode(KeyTableModel::TypeColumn,
                                                        QHeaderView::Fixed);
    m_keyTable->horizontalHeader()->setSectionResizeMode(KeyTableModel::TtlColumn,
                                                        QHeaderView::Fixed);
    m_keyTable->setColumnWidth(KeyTableModel::TypeColumn, 80);
    m_keyTable->setColumnWidth(KeyTableModel::TtlColumn, 110);
    m_keyTable->verticalHeader()->setVisible(false);

    m_treeView = new QTreeView(m_keyViews);
    m_treeView->setObjectName(QStringLiteral("keyTree"));
    m_treeView->setUniformRowHeights(true);
    m_treeView->setAlternatingRowColors(true);
    m_treeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_treeView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_treeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_treeView->setIndentation(16);
    m_treeModel = new NamespaceTreeModel(this);
    m_treeView->setModel(m_treeModel);
    m_treeView->header()->setMinimumSectionSize(64);
    m_treeView->header()->setStretchLastSection(false);
    m_treeView->header()->setSectionResizeMode(NamespaceTreeModel::NameColumn,
                                               QHeaderView::Stretch);
    m_treeView->header()->setSectionResizeMode(NamespaceTreeModel::TypeColumn,
                                               QHeaderView::Fixed);
    m_treeView->header()->setSectionResizeMode(NamespaceTreeModel::TtlColumn,
                                               QHeaderView::Fixed);
    m_treeView->setColumnWidth(NamespaceTreeModel::TypeColumn, 100);
    m_treeView->setColumnWidth(NamespaceTreeModel::TtlColumn, 100);

    m_keyViews->addWidget(m_treeView);
    m_keyViews->addWidget(m_keyTable);
    m_keyViews->setCurrentIndex(0);
    keyLayout->addWidget(m_keyViews);
    splitter->addWidget(keyPane);

    m_valuePanel = new ValuePanel(&m_router, splitter);
    m_valuePanel->setObjectName(QStringLiteral("valuePanel"));
    m_valuePanel->setMinimumWidth(420);
    splitter->addWidget(m_valuePanel);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes({500, 700});
    browserLayout->addWidget(splitter, 1);

    m_tabs->addTab(browser, tr("Browser"));

    auto *cli = new QWidget(m_tabs);
    auto *cliLayout = new QVBoxLayout(cli);
    m_output = new QPlainTextEdit(cli);
    m_output->setReadOnly(true);
    m_output->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_output->setMaximumBlockCount(10000);
    cliLayout->addWidget(m_output, 1);

    auto *commandLayout = new QHBoxLayout;
    m_commandEdit = new QLineEdit(cli);
    m_commandEdit->setPlaceholderText(tr("Command, e.g. PING / INFO / GET key"));
    m_commandEdit->setEnabled(false);
    commandLayout->addWidget(m_commandEdit, 1);
    m_sendButton = new QPushButton(tr("Send"), cli);
    m_sendButton->setEnabled(false);
    commandLayout->addWidget(m_sendButton);
    cliLayout->addLayout(commandLayout);
    m_tabs->addTab(cli, tr("CLI"));

    m_tools = new ServerToolsPage(&m_router, m_tabs);
    m_tabs->addTab(m_tools, tr("Tools"));

    layout->addWidget(m_tabs, 1);
    setCentralWidget(central);

    m_connectionsDock = new QDockWidget(tr("Connections"), this);
    QDockWidget *connectionsDock = m_connectionsDock;
    connectionsDock->setObjectName(QStringLiteral("connectionsDock"));
    auto *connectionsPanel = new QWidget(connectionsDock);
    auto *connectionsLayout = new QVBoxLayout(connectionsPanel);
    connectionsLayout->setContentsMargins(6, 6, 6, 6);
    m_profileEmptyLabel = new QLabel(tr("No saved connections. Click New to create one."),
                                     connectionsPanel);
    m_profileEmptyLabel->setAlignment(Qt::AlignCenter);
    m_profileEmptyLabel->setWordWrap(true);
    m_profileEmptyLabel->setObjectName(QStringLiteral("profileEmptyLabel"));
    connectionsLayout->addWidget(m_profileEmptyLabel);
    m_profileList = new QTreeWidget(connectionsPanel);
    m_profileList->setObjectName(QStringLiteral("profileList"));
    m_profileList->setHeaderHidden(true);
    m_profileList->setRootIsDecorated(true);
    m_profileList->setIndentation(14);
    m_profileList->setAlternatingRowColors(true);
    m_profileList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_profileList->setSelectionBehavior(QAbstractItemView::SelectRows);
    connectionsLayout->addWidget(m_profileList, 1);
    auto *profileButtons = new QHBoxLayout;
    auto *newProfileButton = new QPushButton(tr("New"), connectionsPanel);
    m_profileDuplicateButton = new QPushButton(tr("Duplicate"), connectionsPanel);
    m_profileDuplicateButton->setObjectName(QStringLiteral("duplicateProfileButton"));
    m_profileEditButton = new QPushButton(tr("Edit"), connectionsPanel);
    m_profileEditButton->setObjectName(QStringLiteral("editProfileButton"));
    m_profileDeleteButton = new QPushButton(tr("Delete"), connectionsPanel);
    m_profileDeleteButton->setObjectName(QStringLiteral("deleteProfileButton"));
    m_profileConnectButton = new QPushButton(tr("Connect"), connectionsPanel);
    m_profileConnectButton->setObjectName(QStringLiteral("profileConnectButton"));
    m_profileNewWindowButton = new QPushButton(tr("New Tab"), connectionsPanel);
    m_profileNewWindowButton->setObjectName(QStringLiteral("profileNewWindowButton"));
    m_profileNewWindowButton->setToolTip(tr("Open the selected connection in a new tab"));
    profileButtons->addWidget(newProfileButton);
    profileButtons->addWidget(m_profileDuplicateButton);
    profileButtons->addWidget(m_profileEditButton);
    profileButtons->addWidget(m_profileDeleteButton);
    profileButtons->addWidget(m_profileConnectButton);
    profileButtons->addWidget(m_profileNewWindowButton);
    connectionsLayout->addLayout(profileButtons);
    m_readOnlyDock = new QCheckBox(tr("Read-only"), connectionsPanel);
    m_readOnlyDock->setObjectName(QStringLiteral("readOnlyDock"));
    m_readOnlyDock->setToolTip(tr("Disable editing and CLI write access for this session"));
    connectionsLayout->addWidget(m_readOnlyDock);
    connectionsDock->setWidget(connectionsPanel);
    addDockWidget(Qt::LeftDockWidgetArea, connectionsDock);
    connect(connectionsDock, &QDockWidget::visibilityChanged, this,
            [this](bool visible) { emit connectionsDockVisibilityChanged(visible); });
    QAction *dockToggle = connectionsDock->toggleViewAction();
    dockToggle->setObjectName(QStringLiteral("connectionsDockAction"));
    dockToggle->setText(tr("Connections Dock"));

    m_hostEdit->setAccessibleName(tr("Host"));
    m_portSpin->setAccessibleName(tr("Port"));
    m_usernameEdit->setAccessibleName(tr("ACL user"));
    m_passwordEdit->setAccessibleName(tr("Password"));
    m_dbSpin->setAccessibleName(tr("Database"));
    m_filterEdit->setAccessibleName(tr("SCAN pattern"));
    m_typeCombo->setAccessibleName(tr("Key type filter"));
    m_keyTable->setAccessibleName(tr("Keys"));
    m_treeView->setAccessibleName(tr("Namespace tree"));
    m_viewMode->setAccessibleName(tr("View mode"));
    m_dbCombo->setAccessibleName(tr("Database"));

    auto *focusFilter = new QShortcut(QKeySequence::Find, this);
    connect(focusFilter, &QShortcut::activated, this, [this] {
        m_tabs->setCurrentIndex(1);
        m_filterEdit->setFocus();
        m_filterEdit->selectAll();
    });
    auto *refreshView = new QShortcut(QKeySequence::Refresh, this);
    connect(refreshView, &QShortcut::activated, this, [this] {
        if (m_router.isReady())
            startScan();
    });

    m_scanner = new KeyScanner(&m_router, this);

    connect(newProfileButton, &QPushButton::clicked, this, &MainWindow::createProfile);
    connect(m_profileDuplicateButton, &QPushButton::clicked, this, &MainWindow::duplicateProfile);
    connect(m_profileEditButton, &QPushButton::clicked, this, &MainWindow::editProfile);
    connect(m_profileDeleteButton, &QPushButton::clicked, this, &MainWindow::deleteProfile);
    connect(m_profileConnectButton, &QPushButton::clicked, this, &MainWindow::connectProfile);
    connect(m_profileNewWindowButton, &QPushButton::clicked, this,
            &MainWindow::openActiveProfileInNewWindow);
    connect(m_readOnlyDock, &QCheckBox::toggled, this, [this](bool readOnly) {
        const QSignalBlocker blocker(m_readOnlyCheck);
        m_readOnlyCheck->setChecked(readOnly);
        emit readOnlyChanged(readOnly);
        m_valuePanel->setReadOnly(readOnly);
        if (ConnectionProfile *selected = selectedProfile()) {
            selected->readOnly = readOnly;
            saveProfiles();
        }
        if (m_router.isReady()) {
            m_sendButton->setEnabled(!readOnly);
            m_commandEdit->setEnabled(!readOnly);
            m_statusLabel->setText(readOnly ? tr("Read-only mode") : tr("Ready"));
        }
    });
    connect(m_profileList, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item, int) {
        if (item && !item->data(0, Qt::UserRole).toString().isEmpty())
            connectProfile();
    });
    connect(m_profileList, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *current, QTreeWidgetItem *) {
                if (!current)
                    return;
                const QString id = current->data(0, Qt::UserRole).toString();
                if (id.isEmpty())
                    return;
                for (const ConnectionProfile &profile : std::as_const(m_profiles)) {
                    if (profile.id == id) {
                        applyProfile(profile);
                        break;
                    }
                }
                updateProfileButtons();
            });
    connect(m_profileList, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem *item) {
        if (item && item->data(0, Qt::UserRole).toString().isEmpty())
            m_collapsedGroups.insert(item->text(0));
    });
    connect(m_profileList, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *item) {
        if (item && item->data(0, Qt::UserRole).toString().isEmpty())
            m_collapsedGroups.remove(item->text(0));
    });
    connect(m_connectButton, &QPushButton::clicked, this, &MainWindow::connectOrDisconnect);
    connect(m_sendButton, &QPushButton::clicked, this, &MainWindow::sendCommand);
    connect(m_commandEdit, &QLineEdit::returnPressed, this, &MainWindow::sendCommand);
    connect(m_scanButton, &QPushButton::clicked, this, &MainWindow::startScan);
    connect(m_loadMoreButton, &QPushButton::clicked, this, &MainWindow::loadMore);
    connect(m_indexAllButton, &QPushButton::clicked, this, [this] {
        if (!m_router.isReady() || m_scanner->isRunning())
            return;
        if (QMessageBox::question(
                this, tr("Index all keys"),
                tr("This scans the entire database and keeps every key in memory "
                   "for exact namespace counts. Large databases can use a lot of memory.\n\n"
                   "Continue?"))
            != QMessageBox::Yes) {
            return;
        }
        m_indexingAll = true;
        m_model->clear();
        m_treeModel->clear();
        m_valuePanel->clearKey();
        m_scanStatus->setText(tr("Indexing all keys..."));
        m_scanButton->setEnabled(false);
        m_loadMoreButton->setEnabled(false);
        m_indexAllButton->setEnabled(false);
        const QByteArray pattern = m_filterEdit->text().trimmed().isEmpty()
            ? QByteArrayLiteral("*")
            : m_filterEdit->text().trimmed().toUtf8();
        m_scanner->startAll(pattern, m_typeCombo->currentData().toString());
    });
    connect(m_filterEdit, &QLineEdit::returnPressed, this, &MainWindow::startScan);
    connect(m_typeCombo, &QComboBox::currentIndexChanged, this, [this] {
        if (m_router.isReady())
            startScan();
    });
    connect(m_dbSpin, &QSpinBox::valueChanged, this, [this](int db) {
        syncDatabaseUi(db);
        if (m_router.isReady())
            selectDatabase(db);
    });
    connect(m_dbCombo, &QComboBox::currentIndexChanged, this, [this] {
        const int db = m_dbCombo->currentData().toInt();
        if (db == m_dbSpin->value()) {
            if (m_router.isReady())
                selectDatabase(db);
        } else {
            m_dbSpin->setValue(db);
        }
    });
    connect(m_viewMode, &QComboBox::currentIndexChanged, this, [this] {
        m_keyViews->setCurrentIndex(m_viewMode->currentData().toInt());
    });
    connect(m_dashboard, &DashboardPage::databaseActivated, this, [this](int db) {
        if (m_dbSpin->value() == db) {
            selectDatabase(db);
            m_tabs->setCurrentIndex(1);
        } else {
            m_dbSpin->setValue(db);
            m_tabs->setCurrentIndex(1);
        }
    });
    connect(m_clusterCheck, &QCheckBox::toggled, this, [this](bool cluster) {
        if (!m_router.isReady()) {
            m_dbSpin->setEnabled(!cluster);
            m_dbCombo->setEnabled(!cluster);
        }
    });
    connect(m_readOnlyCheck, &QCheckBox::toggled, this, [this](bool readOnly) {
        emit readOnlyChanged(readOnly);
        m_valuePanel->setReadOnly(readOnly);
        if (ConnectionProfile *selected = selectedProfile()) {
            selected->readOnly = readOnly;
            saveProfiles();
        }
        if (m_router.isReady()) {
            m_sendButton->setEnabled(!readOnly);
            m_commandEdit->setEnabled(!readOnly);
            m_statusLabel->setText(readOnly ? tr("Read-only mode") : tr("Ready"));
        }
    });
    m_keyTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_keyTable, &QTableView::customContextMenuRequested, this,
            &MainWindow::showKeyContextMenu);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_treeView, &QTreeView::customContextMenuRequested, this,
            &MainWindow::showTreeContextMenu);
    connect(m_keyTable->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            &MainWindow::selectKey);
    connect(m_treeView->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            &MainWindow::selectTreeKey);
    connect(m_treeView, &QTreeView::doubleClicked, this, [this](const QModelIndex &index) {
        if (!index.isValid() || m_treeModel->isKey(index)) {
            if (index.isValid())
                selectTreeKey(index, {});
            return;
        }
        const QString path = index.data(NamespaceTreeModel::PathRole).toString();
        if (path.isEmpty())
            return;
        m_filterEdit->setText(path + m_treeModel->separator() + QStringLiteral("*"));
        m_viewMode->setCurrentIndex(1);
        startScan();
    });

    connect(m_scanner, &KeyScanner::batchReady, this, [this](const QVector<KeyInfo> &keys) {
        m_model->appendKeys(keys);
        m_treeModel->addKeys(keys);
        m_treeView->expandToDepth(1);
        m_scanStatus->setText(tr("Loaded %1 keys (%2 scanned)")
                                  .arg(m_model->count())
                                  .arg(m_scanner->scanned()));
        m_loadMoreButton->setEnabled(!m_scanner->isFinished() && !m_indexingAll);
        m_scanButton->setEnabled(true);
        if (m_model->count() > 0 && m_keyViews->currentWidget() == m_keyTable
            && !m_keyTable->currentIndex().isValid()) {
            m_keyTable->selectRow(0);
        } else if (m_model->count() > 0 && m_keyViews->currentWidget() == m_treeView
                   && !m_treeView->currentIndex().isValid()) {
            std::function<QModelIndex(const QModelIndex &)> firstKey =
                [this, &firstKey](const QModelIndex &parent) -> QModelIndex {
                for (int row = 0; row < m_treeModel->rowCount(parent); ++row) {
                    const QModelIndex index = m_treeModel->index(row, 0, parent);
                    if (m_treeModel->isKey(index))
                        return index;
                    const QModelIndex child = firstKey(index);
                    if (child.isValid())
                        return child;
                }
                return {};
            };
            const QModelIndex first = firstKey({});
            if (first.isValid())
                m_treeView->setCurrentIndex(first);
        }
    });
    connect(m_scanner, &KeyScanner::progress, this, [this](qint64 scanned, bool finished) {
        m_scanStatus->setText(finished
                                  ? tr("Scanned %1 keys — complete").arg(scanned)
                                  : tr("Scanned %1 keys").arg(scanned));
        m_loadMoreButton->setEnabled(!finished && !m_scanner->isRunning() && !m_indexingAll);
        m_scanButton->setEnabled(!m_scanner->isRunning() || !m_indexingAll);
        if (finished) {
            m_indexingAll = false;
            m_indexAllButton->setEnabled(true);
        }
    });
    connect(m_scanner, &KeyScanner::errorOccurred, this, [this](const QString &error) {
        m_indexingAll = false;
        m_scanStatus->setText(error);
        m_scanButton->setEnabled(true);
        m_loadMoreButton->setEnabled(false);
        m_indexAllButton->setEnabled(true);
    });

    connect(&m_router, &RedisClient::stateChanged, this, [this](const QString &state) {
        m_statusLabel->setText(state);
        if (state == QLatin1String("Ready")) {
            setConnectedUi(true);
            syncDatabaseUi(m_dbSpin->value());
            m_dashboard->setConnected(true);
            m_tools->setConnectionParameters(
                m_effectiveHost.isEmpty() ? m_hostEdit->text().trimmed() : m_effectiveHost,
                m_effectivePort == 0 ? static_cast<quint16>(m_portSpin->value())
                                     : m_effectivePort,
                m_tlsCheck->isChecked(), m_usernameEdit->text(), m_passwordEdit->text(),
                m_dbSpin->value());
            m_tools->setConnected(true);
            m_retryButton->hide();
            m_authPromptTried = false;
            if (m_clusterCheck->isChecked())
                appendOutput(tr("[cluster] Browser and Dashboard aggregate all masters; "
                                "server tools still inspect one node."));
            startScan();
        } else if (state == QLatin1String("Disconnected")) {
            if (m_sshTunnel)
                m_sshTunnel->stop();
            setConnectedUi(false);
            m_dashboard->setConnected(false);
            m_tools->setConnected(false);
            m_connectButton->setText(tr("Connect"));
            m_connectButton->setEnabled(true);
        } else {
            m_connectButton->setText(tr("Connecting..."));
            m_connectButton->setEnabled(false);
            m_retryButton->hide();
        }
    });
    connect(&m_router, &RedisClient::errorOccurred, this, [this](const QString &error) {
        appendOutput(QStringLiteral("[error] ") + error);
        QString displayError = error;
        if (error.contains(QStringLiteral("NOPERM"), Qt::CaseInsensitive)
            || error.contains(QStringLiteral("READONLY"), Qt::CaseInsensitive)
            || error.contains(QStringLiteral("MISCONF"), Qt::CaseInsensitive)) {
            displayError += QLatin1Char('\n')
                + tr("The server rejected this operation (ACL or server read-only mode).");
        }
        m_statusLabel->setText(displayError);
        m_statusLabel->setToolTip(displayError);
        m_retryButton->show();
        setConnectedUi(false);
        m_dashboard->setConnected(false);
        m_tools->setConnected(false);
        m_connectButton->setText(tr("Connect"));
        m_connectButton->setEnabled(true);

        const bool authRequired = error.contains(QStringLiteral("NOAUTH"), Qt::CaseInsensitive)
            || error.contains(QStringLiteral("WRONGPASS"), Qt::CaseInsensitive);
        if (authRequired && !m_authPromptTried
            && !error.contains(QStringLiteral("without any password configured"),
                               Qt::CaseInsensitive)) {
            m_authPromptTried = true;
            bool ok = false;
            const QString password = QInputDialog::getText(
                this, tr("Authentication required"),
                tr("Redis requested authentication. Enter the password:"),
                QLineEdit::Password, {}, &ok);
            if (ok) {
                m_passwordEdit->setText(password);
                if (ConnectionProfile *selected = selectedProfile(); selected
                    && !password.isEmpty()) {
                    selected->password = password;
                    SecretStore secrets;
                    if (secrets.isAvailable())
                        secrets.writePassword(selected->id, password);
                }
                connectOrDisconnect(true);
            }
        }
    });
    connect(&m_router, &RedisClient::replyReady, this, [this](const RespValue &reply) {
        if (reply.type == RespType::BulkString || reply.type == RespType::SimpleString) {
            appendOutput(QString::fromUtf8(reply.scalar));
        } else {
            appendOutput(reply.toDisplayString());
        }
    });
    connect(&m_router, &RedisClient::pushReceived, this, [this](const RespValue &reply) {
        appendOutput(QStringLiteral("[push] ") + reply.toDisplayString());
    });

    connect(m_valuePanel, &ValuePanel::keyDeleted, this, [this](const QByteArray &key) {
        m_model->removeKey(key);
        m_treeModel->removeKey(key);
        m_scanStatus->setText(tr("Key deleted: %1").arg(QString::fromUtf8(key)));
    });
    connect(m_valuePanel, &ValuePanel::keyChanged, this, [this](const QByteArray &key) {
        const int row = m_model->indexOf(key);
        if (row >= 0)
            m_treeModel->updateKey(m_model->keyAt(row));
    });
}

void MainWindow::loadProfiles()
{
    ProfileStore store;
    QString error;
    if (!store.load(&m_profiles, &error)) {
        m_scanStatus->setText(error);
        return;
    }
    if (!m_profiles.isEmpty())
        m_activeProfileId = m_profiles.constFirst().id;
    rebuildProfileList();
    if (const ConnectionProfile *profile = selectedProfile())
        applyProfile(*profile);
    else if (!m_profiles.isEmpty())
        applyProfile(m_profiles.constFirst());
}

void MainWindow::rebuildProfileList()
{
    if (!m_profileList)
        return;
    const QSignalBlocker blocker(m_profileList);
    m_profileList->clear();

    QVector<ConnectionProfile> sorted = m_profiles;
    std::sort(sorted.begin(), sorted.end(),
              [](const ConnectionProfile &left, const ConnectionProfile &right) {
                  const int group = QString::compare(left.group, right.group, Qt::CaseInsensitive);
                  if (group != 0)
                      return group < 0;
                  return QString::compare(left.name, right.name, Qt::CaseInsensitive) < 0;
              });

    QString currentGroup;
    QTreeWidgetItem *groupItem = nullptr;
    QTreeWidgetItem *activeItem = nullptr;
    for (const ConnectionProfile &profile : std::as_const(sorted)) {
        if (profile.group != currentGroup) {
            currentGroup = profile.group;
            groupItem = new QTreeWidgetItem(m_profileList);
            groupItem->setText(0, currentGroup);
            groupItem->setData(0, Qt::UserRole, QString());
            groupItem->setFlags(Qt::ItemIsEnabled);
            QFont font = groupItem->font(0);
            font.setBold(true);
            groupItem->setFont(0, font);
            groupItem->setExpanded(!m_collapsedGroups.contains(currentGroup));
        }
        auto *item = new QTreeWidgetItem(groupItem);
        item->setText(0, profile.name);
        item->setData(0, Qt::UserRole, profile.id);
        item->setToolTip(0, QStringLiteral("%1:%2").arg(profile.host).arg(profile.port));
        const QColor color(profile.color);
        if (color.isValid()) {
            QPixmap pixmap(12, 12);
            pixmap.fill(color);
            item->setIcon(0, QIcon(pixmap));
        } else {
            item->setIcon(0, QIcon::fromTheme(QStringLiteral("network-server")));
        }
        if (profile.id == m_activeProfileId)
            activeItem = item;
    }
    if (activeItem)
        m_profileList->setCurrentItem(activeItem);
    if (m_profileEmptyLabel)
        m_profileEmptyLabel->setVisible(m_profiles.isEmpty());
    updateProfileButtons();
}

void MainWindow::applyProfile(const ConnectionProfile &profile)
{
    m_activeProfileId = profile.id;
    m_hostEdit->setText(profile.host);
    m_portSpin->setValue(profile.port);
    m_tlsCheck->setChecked(profile.tls);
    m_usernameEdit->setText(profile.username);
    m_passwordEdit->setText(profile.password);
    m_dbSpin->setValue(profile.db);
    m_readOnlyCheck->setChecked(profile.readOnly);
    if (m_readOnlyDock) {
        const QSignalBlocker blocker(m_readOnlyDock);
        m_readOnlyDock->setChecked(profile.readOnly);
    }
    m_clusterCheck->setChecked(profile.cluster);
    m_passwordEdit->setText(profile.password);
    syncDatabaseUi(profile.db);
}

void MainWindow::createProfile()
{
    ConnectionProfile profile;
    profile.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    profile.name = tr("New connection");
    profile.host = m_hostEdit->text().trimmed().isEmpty() ? QStringLiteral("127.0.0.1")
                                                          : m_hostEdit->text().trimmed();
    profile.port = static_cast<quint16>(m_portSpin->value());
    profile.tls = m_tlsCheck->isChecked();
    profile.username = m_usernameEdit->text();
    profile.db = m_dbSpin->value();
    profile.readOnly = m_readOnlyCheck->isChecked();

    ProfileDialog dialog(profile, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    profile = dialog.profile();
    m_profiles.append(profile);
    m_activeProfileId = profile.id;
    saveProfiles();
    rebuildProfileList();
    applyProfile(profile);
}

void MainWindow::editProfile()
{
    ConnectionProfile *selected = selectedProfile();
    if (!selected)
        return;
    ProfileDialog dialog(*selected, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const ConnectionProfile updated = dialog.profile();
    for (ConnectionProfile &profile : m_profiles) {
        if (profile.id == updated.id) {
            profile = updated;
            break;
        }
    }
    m_activeProfileId = updated.id;
    saveProfiles();
    rebuildProfileList();
    applyProfile(updated);
}

void MainWindow::deleteProfile()
{
    ConnectionProfile *selected = selectedProfile();
    if (!selected)
        return;
    if (QMessageBox::question(this, tr("Delete connection"),
                              tr("Delete saved connection \"%1\"?").arg(selected->name))
        != QMessageBox::Yes) {
        return;
    }
    const QString id = selected->id;
    for (int index = 0; index < m_profiles.size(); ++index) {
        if (m_profiles.at(index).id == id) {
            m_profiles.removeAt(index);
            break;
        }
    }
    if (m_activeProfileId == id)
        m_activeProfileId.clear();
    SecretStore secrets;
    if (secrets.isAvailable()) {
        secrets.deletePassword(id);
        secrets.deleteSshPassword(id);
    }
    saveProfiles();
    rebuildProfileList();
}

void MainWindow::duplicateProfile()
{
    ConnectionProfile *selected = selectedProfile();
    if (!selected)
        return;
    ConnectionProfile copy = *selected;
    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    copy.name = tr("%1 copy").arg(selected->name);
    m_profiles.append(copy);
    m_activeProfileId = copy.id;
    saveProfiles();
    rebuildProfileList();
    applyProfile(copy);
}

void MainWindow::updateProfileButtons()
{
    if (!m_profileEditButton || !m_profileDeleteButton || !m_profileDuplicateButton
        || !m_profileConnectButton) {
        return;
    }
    const bool selected = selectedProfile() != nullptr;
    m_profileEditButton->setEnabled(selected);
    m_profileDeleteButton->setEnabled(selected);
    m_profileDuplicateButton->setEnabled(selected);
    m_profileConnectButton->setEnabled(selected || m_router.isReady());
    if (m_profileNewWindowButton)
        m_profileNewWindowButton->setEnabled(selected);
}

void MainWindow::newWindow()
{
    if (receivers(SIGNAL(newSessionRequested())) > 0) {
        emit newSessionRequested();
        return;
    }
    auto *window = new MainWindow;
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
    window->raise();
    window->activateWindow();
}

void MainWindow::connectProfileById(const QString &profileId)
{
    m_activeProfileId = profileId;
    rebuildProfileList();
    connectProfile();
}

void MainWindow::openActiveProfileInNewWindow()
{
    ConnectionProfile *profile = selectedProfile();
    if (!profile)
        return;
    openProfileInNewWindow(profile->id);
}

void MainWindow::openProfileInNewWindow(const QString &profileId)
{
    if (receivers(SIGNAL(newSessionWithProfileRequested(QString))) > 0) {
        emit newSessionWithProfileRequested(profileId);
        return;
    }
    const auto it = std::find_if(m_profiles.constBegin(), m_profiles.constEnd(),
                                 [&profileId](const ConnectionProfile &profile) {
                                     return profile.id == profileId;
                                 });
    if (it == m_profiles.constEnd())
        return;
    auto *window = new MainWindow;
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->m_profiles = m_profiles;
    window->m_activeProfileId = profileId;
    window->rebuildProfileList();
    window->show();
    window->raise();
    window->activateWindow();
    window->connectProfile();
}

void MainWindow::connectProfile()
{
    if (m_router.isReady()) {
        if (m_sshTunnel)
            m_sshTunnel->stop();
        if (m_sentinelWatcher)
            m_sentinelWatcher->stop();
        m_router.disconnectFromHost();
        return;
    }
    ConnectionProfile *selected = selectedProfile();
    if (!selected)
        return;
    applyProfile(*selected);
    connectOrDisconnect();
}

void MainWindow::saveProfiles()
{
    ProfileStore store;
    QString error;
    if (!store.save(m_profiles, &error))
        m_scanStatus->setText(error);
}

ConnectionProfile *MainWindow::selectedProfile()
{
    if (!m_profileList)
        return nullptr;
    QTreeWidgetItem *item = m_profileList->currentItem();
    if (!item)
        return nullptr;
    const QString id = item->data(0, Qt::UserRole).toString();
    if (id.isEmpty())
        return nullptr;
    for (ConnectionProfile &profile : m_profiles) {
        if (profile.id == id)
            return &profile;
    }
    return nullptr;
}

void MainWindow::connectOrDisconnect(bool retryAuth)
{
    if (m_connectButton->text() == tr("Disconnect")) {
        if (m_sshTunnel)
            m_sshTunnel->stop();
        m_router.disconnectFromHost();
        return;
    }
    if (m_sentinelWatcher)
        m_sentinelWatcher->stop();
    if (!retryAuth)
        m_authPromptTried = false;

    ConnectionProfile profile;
    if (ConnectionProfile *selected = selectedProfile()) {
        selected->host = m_hostEdit->text().trimmed();
        selected->port = static_cast<quint16>(m_portSpin->value());
        selected->tls = m_tlsCheck->isChecked();
        selected->username = m_usernameEdit->text();
        selected->db = m_dbSpin->value();
        selected->readOnly = m_readOnlyCheck->isChecked();
        selected->cluster = m_clusterCheck->isChecked();
        profile = *selected;
        saveProfiles();
    } else {
        // The top bar can make an ephemeral connection; saved connections are
        // created explicitly from the left-hand Connections dock.
        profile.host = m_hostEdit->text().trimmed();
        profile.port = static_cast<quint16>(m_portSpin->value());
        profile.tls = m_tlsCheck->isChecked();
        profile.username = m_usernameEdit->text();
        profile.db = m_dbSpin->value();
        profile.readOnly = m_readOnlyCheck->isChecked();
        profile.cluster = m_clusterCheck->isChecked();
        profile.unixSocketPath = m_ephemeralUnixSocket;
    }

    emit sessionTitleChanged(profile.name.isEmpty()
                                 ? tr("Connection %1").arg(profile.host)
                                 : profile.name);

    SecretStore secrets;
    if (secrets.isAvailable() && !profile.id.isEmpty()) {
        if (m_passwordEdit->text().isEmpty()) {
            QString secretError;
            const QString stored = secrets.readPassword(profile.id, &secretError);
            if (!stored.isEmpty())
                m_passwordEdit->setText(stored);
        }
        if (!m_passwordEdit->text().isEmpty()) {
            QString secretError;
            if (!secrets.writePassword(profile.id, m_passwordEdit->text(), &secretError))
                appendOutput(QStringLiteral("[warning] ") + secretError);
        }
    }

    if (profile.sshTunnel && !profile.id.isEmpty()) {
        if (profile.sshPassword.isEmpty()) {
            QString secretError;
            const QString stored = secrets.readSshPassword(profile.id, &secretError);
            if (!stored.isEmpty())
                profile.sshPassword = stored;
        }
        if (!profile.sshPassword.isEmpty()) {
            QString secretError;
            if (!secrets.writeSshPassword(profile.id, profile.sshPassword, &secretError))
                appendOutput(QStringLiteral("[warning] ") + secretError);
        }
    }

    if (profile.cluster && profile.unixSocketPath.isEmpty()) {
        m_router.setBackend(std::make_unique<ClusterClient>());
    } else {
        m_router.setBackend(std::make_unique<RedisConnection>());
    }
    m_router.setTlsConfig(tlsConfigForProfile(profile));

    m_output->clear();
    m_model->clear();
    m_valuePanel->clearKey();
    m_dashboard->setConnected(false);
    m_tools->setConnected(false);
    m_scanStatus->setText(tr("Connecting..."));
    appendOutput(tr("Connecting..."));
    m_connectButton->setEnabled(false);
    if (!profile.unixSocketPath.isEmpty()) {
        m_effectiveHost = profile.unixSocketPath;
        m_effectivePort = 0;
        m_scanStatus->setText(tr("Connecting to Unix socket..."));
        m_router.connectToUnixSocket(profile.unixSocketPath, profile.username,
                                     m_passwordEdit->text(), profile.db);
        return;
    }
    if (profile.sshTunnel) {
        if (m_sshTunnel)
            m_sshTunnel->stop();
        m_scanStatus->setText(tr("Opening SSH tunnel..."));
        m_sshTunnel = new SshTunnel(this);
        connect(m_sshTunnel, &SshTunnel::ready, this,
                [this, profile](quint16 localPort) {
            if (profile.cluster) {
                m_scanStatus->setText(tr("SSH SOCKS proxy ready; connecting to cluster..."));
                m_router.setNetworkProxy(
                    QNetworkProxy(QNetworkProxy::Socks5Proxy, QStringLiteral("127.0.0.1"),
                                  localPort));
                m_effectiveHost = profile.host;
                m_effectivePort = profile.port;
                m_router.connectToHost(profile.host, profile.port, profile.tls,
                                       profile.username, m_passwordEdit->text(), profile.db);
                return;
            }
            m_scanStatus->setText(tr("SSH tunnel ready; connecting..."));
            m_effectiveHost = QStringLiteral("127.0.0.1");
            m_effectivePort = localPort;
            m_router.connectToHost(m_effectiveHost, m_effectivePort, profile.tls,
                                   profile.username, m_passwordEdit->text(), profile.db);
        });
        connect(m_sshTunnel, &SshTunnel::failed, this, [this](const QString &error) {
            m_scanStatus->setText(tr("SSH tunnel error: %1").arg(error));
            m_connectButton->setEnabled(true);
            m_connectButton->setText(tr("Connect"));
        });
        if (profile.cluster) {
            m_sshTunnel->startDynamic(profile.sshHost, profile.sshPort, profile.sshUser,
                                      profile.sshKeyPath, profile.sshPassword);
        } else {
            m_sshTunnel->start(profile.sshHost, profile.sshPort, profile.sshUser,
                               profile.sshKeyPath, profile.sshPassword, profile.host, profile.port);
        }
        return;
    }
    if (profile.sentinel) {
        m_scanStatus->setText(tr("Resolving Sentinel master..."));
        auto *resolver = new SentinelResolver(this);
        connect(resolver, &SentinelResolver::resolved, this,
                [this, resolver, profile](const QString &host, quint16 port) {
            m_scanStatus->setText(tr("Connecting to master %1:%2...").arg(host).arg(port));
            m_effectiveHost = host;
            m_effectivePort = port;
            const ConnectionProfile sentinelProfile = profile;
            m_router.connectToHost(host, port, sentinelProfile.tls, sentinelProfile.username,
                                   m_passwordEdit->text(), sentinelProfile.db);
            if (!m_sentinelWatcher) {
                m_sentinelWatcher = new SentinelWatcher(this);
                connect(m_sentinelWatcher, &SentinelWatcher::masterChanged, this,
                        [this, sentinelProfile](const QString &newHost, quint16 newPort) {
                    if (!sentinelProfile.sentinel)
                        return;
                    m_effectiveHost = newHost;
                    m_effectivePort = newPort;
                    m_scanStatus->setText(
                        tr("Sentinel failover: connecting to %1:%2...")
                            .arg(newHost)
                            .arg(newPort));
                    m_router.connectToHost(newHost, newPort, sentinelProfile.tls,
                                           sentinelProfile.username, m_passwordEdit->text(),
                                           sentinelProfile.db);
                });
                connect(m_sentinelWatcher, &SentinelWatcher::errorOccurred, this,
                        [this](const QString &error) {
                    appendOutput(tr("[sentinel] ") + error);
                });
            }
            m_sentinelWatcher->start(sentinelProfile.sentinelHost,
                                     sentinelProfile.sentinelPort,
                                     sentinelProfile.sentinelTls,
                                     sentinelProfile.username, m_passwordEdit->text(),
                                     sentinelProfile.sentinelMaster);
            resolver->deleteLater();
        });
        connect(resolver, &SentinelResolver::failed, this,
                [this, resolver](const QString &error) {
            m_scanStatus->setText(tr("Sentinel error: %1").arg(error));
            m_connectButton->setEnabled(true);
            m_connectButton->setText(tr("Connect"));
            resolver->deleteLater();
        });
        resolver->resolve(profile.sentinelHost, profile.sentinelPort, profile.sentinelTls,
                          m_usernameEdit->text(), m_passwordEdit->text(),
                          profile.sentinelMaster);
        return;
    }
    m_effectiveHost = profile.host;
    m_effectivePort = profile.port;
    m_router.connectToHost(m_effectiveHost, m_effectivePort, profile.tls, profile.username,
                           m_passwordEdit->text(), profile.db);
}

void MainWindow::startScan()
{
    if (!m_router.isReady())
        return;
    m_scanner->stop();
    ++m_keyGeneration;
    m_indexingAll = false;
    m_model->clear();
    m_treeModel->clear();
    m_valuePanel->clearKey();
    const QByteArray pattern = m_filterEdit->text().trimmed().isEmpty()
        ? QByteArrayLiteral("*")
        : m_filterEdit->text().trimmed().toUtf8();
    const QString type = m_typeCombo->currentData().toString();
    m_scanStatus->setText(tr("Scanning..."));
    m_scanButton->setEnabled(false);
    m_loadMoreButton->setEnabled(false);
    m_indexAllButton->setEnabled(false);
    m_scanner->start(pattern, type);
}

void MainWindow::selectDatabase(int db)
{
    if (!m_router.isReady())
        return;
    const int previous = m_dbSpin->value();
    Q_UNUSED(previous)
    m_statusLabel->setText(tr("Selecting database %1...").arg(db));
    m_router.sendCommand({QByteArrayLiteral("SELECT"), QByteArray::number(db)},
                             [this, db](const RespValue &reply, const QString &error) {
        if (!error.isEmpty()) {
            m_statusLabel->setText(error);
            return;
        }
        if (reply.isError()) {
            m_statusLabel->setText(QString::fromUtf8(reply.scalar));
            return;
        }
        m_statusLabel->setText(tr("Database %1 selected").arg(db));
        m_dashboard->setConnected(true);
        startScan();
    });
}

void MainWindow::loadMore()
{
    if (!m_router.isReady())
        return;
    m_loadMoreButton->setEnabled(false);
    m_scanner->loadMore();
}

void MainWindow::selectKey(const QModelIndex &current, const QModelIndex &previous)
{
    Q_UNUSED(previous)
    if (!current.isValid()) {
        m_valuePanel->clearKey();
        return;
    }
    m_valuePanel->loadKey(m_model->keyAt(current.row()));
}

void MainWindow::selectTreeKey(const QModelIndex &current, const QModelIndex &previous)
{
    Q_UNUSED(previous)
    if (!current.isValid()) {
        m_valuePanel->clearKey();
        return;
    }
    if (m_treeModel->isKey(current)) {
        m_valuePanel->loadKey(m_treeModel->keyInfo(current));
    } else {
        m_valuePanel->clearKey();
        const QString path = current.data(NamespaceTreeModel::PathRole).toString();
        const QString count = current.siblingAtColumn(NamespaceTreeModel::TypeColumn)
                                  .data(Qt::DisplayRole)
                                  .toString();
        m_scanStatus->setText(tr("Namespace %1: %2 loaded keys").arg(path, count));
    }
}

void MainWindow::syncDatabaseUi(int db)
{
    if (!m_dbCombo)
        return;
    const int index = m_dbCombo->findData(db);
    if (index < 0 || index == m_dbCombo->currentIndex())
        return;
    const QSignalBlocker blocker(m_dbCombo);
    m_dbCombo->setCurrentIndex(index);
}

QVector<KeyInfo> MainWindow::selectedKeys() const
{
    QVector<KeyInfo> keys;
    if (!m_keyTable)
        return keys;
    const QModelIndexList rows = m_keyTable->selectionModel()->selectedRows();
    for (const QModelIndex &index : rows) {
        const KeyInfo info = m_model->keyAt(index.row());
        if (info.isValid())
            keys.append(info);
    }
    return keys;
}

void MainWindow::showKeyContextMenu(const QPoint &position)
{
    const QModelIndex index = m_keyTable->indexAt(position);
    if (!index.isValid())
        return;
    if (!m_keyTable->selectionModel()->isRowSelected(index.row(), QModelIndex()))
        m_keyTable->selectRow(index.row());

    m_contextKey = KeyInfo{};
    const QVector<KeyInfo> selection = selectedKeys();
    if (selection.size() > 1) {
        QMenu menu(this);
        m_keyDeleteAction->setText(tr("Delete %1 keys...").arg(selection.size()));
        m_keyTtlAction->setText(tr("Set TTL for %1 keys...").arg(selection.size()));
        m_keyDeleteAction->setEnabled(!m_readOnlyCheck->isChecked());
        m_keyTtlAction->setEnabled(!m_readOnlyCheck->isChecked());
        menu.addAction(m_keyDeleteAction);
        menu.addAction(m_keyTtlAction);
        menu.exec(m_keyTable->viewport()->mapToGlobal(position));
        return;
    }

    const KeyInfo info = m_model->keyAt(index.row());
    if (!info.isValid())
        return;
    m_contextKey = info;
    QMenu menu(this);
    m_keyRenameAction->setText(tr("Rename..."));
    m_keyDeleteAction->setText(tr("Delete..."));
    m_keyRenameAction->setEnabled(!m_readOnlyCheck->isChecked());
    m_keyDeleteAction->setEnabled(!m_readOnlyCheck->isChecked());
    m_keyCopyKeyAction->setEnabled(true);
    m_keyCopyValueAction->setEnabled(true);
    m_keyCopyToAction->setEnabled(!m_readOnlyCheck->isChecked() && !m_profiles.isEmpty());
    m_keyRefreshAction->setEnabled(true);
    menu.addAction(m_keyRenameAction);
    menu.addAction(m_keyDeleteAction);
    menu.addSeparator();
    menu.addAction(m_keyCopyKeyAction);
    menu.addAction(m_keyCopyValueAction);
    menu.addAction(m_keyCopyToAction);
    menu.addSeparator();
    menu.addAction(m_keyRefreshAction);
    menu.exec(m_keyTable->viewport()->mapToGlobal(position));
    m_contextKey = KeyInfo{};
}

void MainWindow::deleteSelectedKeys()
{
    if (m_readOnlyCheck->isChecked()) {
        m_scanStatus->setText(tr("Read-only mode: delete is disabled"));
        return;
    }
    const QVector<KeyInfo> keys = selectedKeys();
    if (keys.isEmpty())
        return;
    QString sample;
    for (int index = 0; index < keys.size() && index < 5; ++index)
        sample += QString::fromUtf8(keys.at(index).key) + QLatin1Char('\n');
    if (QMessageBox::question(
            this, tr("Delete keys"),
            tr("Delete %1 selected keys?\n\n%2").arg(keys.size()).arg(sample)
                + (keys.size() > 5 ? tr("...") : QString()))
        != QMessageBox::Yes) {
        return;
    }

    struct BatchState {
        int remaining = 0;
        int deleted = 0;
        int failed = 0;
        quint64 generation = 0;
    };
    auto state = std::make_shared<BatchState>();
    state->remaining = keys.size();
    state->generation = m_keyGeneration;
    m_scanStatus->setText(tr("Deleting %1 keys...").arg(keys.size()));

    for (const KeyInfo &info : keys) {
        const QByteArray key = info.key;
        auto finish = [this, state, key](bool success) {
            if (state->generation != m_keyGeneration)
                return;
            if (success) {
                ++state->deleted;
                removeKeyFromViews(key);
                if (m_valuePanel->currentKey() == key)
                    m_valuePanel->clearKey();
            } else {
                ++state->failed;
            }
            if (--state->remaining == 0) {
                m_scanStatus->setText(
                    state->failed == 0
                        ? tr("Deleted %1 keys").arg(state->deleted)
                        : tr("Deleted %1 keys; %2 failed")
                              .arg(state->deleted)
                              .arg(state->failed));
            }
        };

        m_router.sendCommand({QByteArrayLiteral("UNLINK"), key},
                             [this, key, finish](const RespValue &reply, const QString &error) {
            if (!error.isEmpty()) {
                finish(false);
                return;
            }
            if (reply.isError()) {
                m_router.sendCommand({QByteArrayLiteral("DEL"), key},
                                     [this, finish](const RespValue &fallback,
                                                    const QString &fallbackError) {
                    if (!fallbackError.isEmpty() || fallback.isError())
                        finish(false);
                    else
                        finish(true);
                });
                return;
            }
            finish(true);
        });
    }
}

void MainWindow::setSelectedKeysTtl()
{
    if (m_readOnlyCheck->isChecked()) {
        m_scanStatus->setText(tr("Read-only mode: TTL is disabled"));
        return;
    }
    const QVector<KeyInfo> keys = selectedKeys();
    if (keys.isEmpty())
        return;
    bool ok = false;
    const int seconds = QInputDialog::getInt(
        this, tr("Set TTL for keys"), tr("TTL in seconds (-1 = persist):"), -1, -1,
        2147483647, 1, &ok);
    if (!ok)
        return;

    struct BatchState {
        int remaining = 0;
        int updated = 0;
        int failed = 0;
        quint64 generation = 0;
    };
    auto state = std::make_shared<BatchState>();
    state->remaining = keys.size();
    state->generation = m_keyGeneration;
    m_scanStatus->setText(tr("Updating TTL for %1 keys...").arg(keys.size()));

    for (const KeyInfo &info : keys) {
        const QList<QByteArray> command = seconds < 0
            ? QList<QByteArray>{QByteArrayLiteral("PERSIST"), info.key}
            : QList<QByteArray>{QByteArrayLiteral("EXPIRE"), info.key,
                                QByteArray::number(seconds)};
        m_router.sendCommand(command, [this, state, info, seconds](const RespValue &reply,
                                                                   const QString &error) {
            if (state->generation != m_keyGeneration)
                return;
            bool success = error.isEmpty() && !reply.isError();
            if (success && seconds >= 0 && reply.type == RespType::Integer
                && reply.integer == 0) {
                success = false;
            }
            if (success) {
                KeyInfo updated = info;
                updated.ttlMs = seconds < 0 ? -1 : qint64(seconds) * 1000;
                m_model->updateKey(updated);
                m_treeModel->updateKey(updated);
                if (m_valuePanel->currentKey() == info.key)
                    m_valuePanel->loadKey(updated);
                ++state->updated;
            } else {
                ++state->failed;
            }
            if (--state->remaining == 0) {
                m_scanStatus->setText(
                    state->failed == 0
                        ? tr("Updated TTL for %1 keys").arg(state->updated)
                        : tr("Updated TTL for %1 keys; %2 failed")
                              .arg(state->updated)
                              .arg(state->failed));
            }
        });
    }
}

void MainWindow::showTreeContextMenu(const QPoint &position)
{
    const QModelIndex index = m_treeView->indexAt(position);
    if (!index.isValid())
        return;

    if (m_treeModel->isKey(index)) {
        m_contextKey = m_treeModel->keyInfo(index);
        QMenu menu(this);
        m_keyRenameAction->setText(tr("Rename..."));
        m_keyDeleteAction->setText(tr("Delete..."));
        m_keyRenameAction->setEnabled(!m_readOnlyCheck->isChecked());
        m_keyDeleteAction->setEnabled(!m_readOnlyCheck->isChecked());
        m_keyCopyToAction->setEnabled(!m_readOnlyCheck->isChecked() && !m_profiles.isEmpty());
        m_keyCopyKeyAction->setEnabled(true);
        m_keyCopyValueAction->setEnabled(true);
        menu.addAction(m_keyRenameAction);
        menu.addAction(m_keyDeleteAction);
        menu.addSeparator();
        menu.addAction(m_keyCopyKeyAction);
        menu.addAction(m_keyCopyValueAction);
        menu.addAction(m_keyCopyToAction);
        menu.exec(m_treeView->viewport()->mapToGlobal(position));
        m_contextKey = KeyInfo{};
        return;
    }

    const QString path = index.data(NamespaceTreeModel::PathRole).toString();
    if (path.isEmpty())
        return;
    m_contextPath = path;
    QMenu menu(this);
    menu.addAction(m_namespaceFilterAction);
    menu.addAction(m_copyPrefixAction);
    menu.exec(m_treeView->viewport()->mapToGlobal(position));
    m_contextPath.clear();
}

void MainWindow::renameKey(const KeyInfo &info)
{
    if (m_readOnlyCheck->isChecked()) {
        m_scanStatus->setText(tr("Read-only mode: rename is disabled"));
        return;
    }
    bool ok = false;
    const QString currentName = QString::fromUtf8(info.key);
    const QString newName = QInputDialog::getText(
        this, tr("Rename key"), tr("New key name:"), QLineEdit::Normal, currentName, &ok);
    if (!ok || newName.isEmpty() || newName == currentName)
        return;

    m_scanStatus->setText(tr("Renaming key..."));
    m_router.sendCommand({QByteArrayLiteral("RENAME"), info.key, newName.toUtf8()},
                             [this, info, newName](const RespValue &reply,
                                                   const QString &error) {
        if (!error.isEmpty() || reply.isError()) {
            m_scanStatus->setText(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
            return;
        }
        removeKeyFromViews(info.key);
        m_valuePanel->clearKey();
        m_scanStatus->setText(tr("Renamed to %1").arg(newName));
        startScan();
    });
}

void MainWindow::deleteKey(const KeyInfo &info)
{
    if (m_readOnlyCheck->isChecked()) {
        m_scanStatus->setText(tr("Read-only mode: delete is disabled"));
        return;
    }
    if (QMessageBox::question(this, tr("Delete key"),
                              tr("Delete key \"%1\"?").arg(QString::fromUtf8(info.key)))
        != QMessageBox::Yes) {
        return;
    }

    const QByteArray key = info.key;
    m_scanStatus->setText(tr("Deleting key..."));
    m_router.sendCommand({QByteArrayLiteral("UNLINK"), key},
                             [this, key](const RespValue &reply, const QString &error) {
        if (!error.isEmpty()) {
            m_scanStatus->setText(error);
            return;
        }
        if (reply.isError()) {
            m_router.sendCommand({QByteArrayLiteral("DEL"), key},
                                     [this, key](const RespValue &fallback,
                                                 const QString &fallbackError) {
                if (!fallbackError.isEmpty() || fallback.isError()) {
                    m_scanStatus->setText(fallbackError.isEmpty()
                                              ? QString::fromUtf8(fallback.scalar)
                                              : fallbackError);
                    return;
                }
                removeKeyFromViews(key);
                m_valuePanel->clearKey();
                m_scanStatus->setText(tr("Key deleted"));
            });
            return;
        }
        removeKeyFromViews(key);
        m_valuePanel->clearKey();
        m_scanStatus->setText(tr("Key deleted"));
    });
}

void MainWindow::copyKey(const KeyInfo &info)
{
    QGuiApplication::clipboard()->setText(QString::fromUtf8(info.key));
    m_scanStatus->setText(tr("Key copied to clipboard"));
}

void MainWindow::copyValue(const KeyInfo &info)
{
    QList<QByteArray> args;
    if (info.type == QLatin1String("string"))
        args = {QByteArrayLiteral("GET"), info.key};
    else if (info.type == QLatin1String("hash"))
        args = {QByteArrayLiteral("HGETALL"), info.key};
    else if (info.type == QLatin1String("list"))
        args = {QByteArrayLiteral("LRANGE"), info.key, QByteArrayLiteral("0"),
                QByteArrayLiteral("-1")};
    else if (info.type == QLatin1String("set"))
        args = {QByteArrayLiteral("SMEMBERS"), info.key};
    else if (info.type == QLatin1String("zset"))
        args = {QByteArrayLiteral("ZRANGE"), info.key, QByteArrayLiteral("0"),
                QByteArrayLiteral("-1"), QByteArrayLiteral("WITHSCORES")};
    else if (info.type == QLatin1String("stream"))
        args = {QByteArrayLiteral("XRANGE"), info.key, QByteArrayLiteral("-"),
                QByteArrayLiteral("+"), QByteArrayLiteral("COUNT"),
                QByteArrayLiteral("1000")};
    else {
        m_scanStatus->setText(tr("Copy value is not supported for type %1").arg(info.type));
        return;
    }

    m_router.sendCommand(args, [this](const RespValue &reply, const QString &error) {
        if (!error.isEmpty() || reply.isError()) {
            m_scanStatus->setText(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
            return;
        }

        QString text;
        if (reply.type == RespType::BulkString || reply.type == RespType::SimpleString) {
            text = QString::fromUtf8(reply.scalar);
        } else if (reply.type == RespType::Map) {
            for (const auto &[key, value] : reply.pairs)
                text += QString::fromUtf8(key.scalar) + QLatin1Char('\t')
                    + QString::fromUtf8(value.scalar) + QLatin1Char('\n');
        } else if (reply.type == RespType::Array || reply.type == RespType::Set) {
            for (const RespValue &element : reply.elements) {
                if (element.type == RespType::Array && element.elements.size() == 2) {
                    text += QString::fromUtf8(element.elements[0].scalar) + QLatin1Char('\t')
                        + QString::fromUtf8(element.elements[1].scalar) + QLatin1Char('\n');
                } else {
                    text += QString::fromUtf8(element.scalar) + QLatin1Char('\n');
                }
            }
        } else {
            text = reply.toDisplayString();
        }
        QGuiApplication::clipboard()->setText(text);
        m_scanStatus->setText(tr("Value copied to clipboard"));
    });
}

void MainWindow::removeKeyFromViews(const QByteArray &key)
{
    m_model->removeKey(key);
    m_treeModel->removeKey(key);
}

void MainWindow::copyKeyToProfile(const KeyInfo &info)
{
    if (m_profiles.isEmpty())
        return;
    QStringList names;
    QVector<ConnectionProfile> targets;
    for (const ConnectionProfile &profile : std::as_const(m_profiles)) {
        if (profile.id == m_activeProfileId)
            continue;
        names.append(QStringLiteral("%1 — %2:%3").arg(profile.name, profile.host).arg(profile.port));
        targets.append(profile);
    }
    if (targets.isEmpty()) {
        m_scanStatus->setText(tr("No other saved connection"));
        return;
    }
    bool ok = false;
    const QString choice = QInputDialog::getItem(this, tr("Copy key to connection"),
                                                 tr("Target connection:"), names, 0, false, &ok);
    if (!ok)
        return;
    const int index = names.indexOf(choice);
    if (index < 0 || index >= targets.size())
        return;
    const ConnectionProfile targetProfile = targets.at(index);
    if (targetProfile.sshTunnel || targetProfile.sentinel || targetProfile.cluster) {
        m_scanStatus->setText(
            tr("Copy to SSH/Sentinel/Cluster connections is not supported yet"));
        return;
    }
    if (QMessageBox::question(
            this, tr("Copy key"),
            tr("Copy \"%1\" to %2:%3 (DB %4)? Existing keys are replaced.")
                .arg(QString::fromUtf8(info.key), targetProfile.host)
                .arg(targetProfile.port)
                .arg(targetProfile.db))
        != QMessageBox::Yes) {
        return;
    }

    QString password;
    SecretStore secrets;
    if (secrets.isAvailable() && !targetProfile.id.isEmpty())
        password = secrets.readPassword(targetProfile.id);
    if (password.isEmpty() && !targetProfile.username.isEmpty()) {
        password = QInputDialog::getText(this, tr("Target password"), tr("Password:"),
                                          QLineEdit::Password, {}, &ok);
        if (!ok)
            return;
    }

    auto *target = new RedisConnection(this);
    QPointer<RedisConnection> targetGuard(target);
    connect(target, &RedisClient::errorOccurred, this,
            [this, targetGuard](const QString &error) {
        if (!targetGuard)
            return;
        m_scanStatus->setText(tr("Copy failed: %1").arg(error));
        targetGuard->deleteLater();
    });
    connect(target, &RedisClient::stateChanged, this,
            [this, targetGuard, info, password, targetProfile](const QString &state) {
        if (!targetGuard || (state != QLatin1String("Ready")
                             && state != QLatin1String("Ready (RESP2)")))
            return;
        QPointer<RedisConnection> guard = targetGuard;
        guard->sendCommand({QByteArrayLiteral("DUMP"), info.key},
                            [this, guard, info](const RespValue &dump, const QString &error) {
            if (!guard)
                return;
            if (!error.isEmpty() || dump.isError()) {
                m_scanStatus->setText(tr("Copy failed: %1")
                                          .arg(error.isEmpty()
                                                   ? QString::fromUtf8(dump.scalar)
                                                   : error));
                guard->disconnectFromHost();
                guard->deleteLater();
                return;
            }
            guard->sendCommand({QByteArrayLiteral("PTTL"), info.key},
                               [this, guard, info, dump](const RespValue &ttl,
                                                         const QString &ttlError) {
                if (!guard)
                    return;
                const qint64 ttlMs = ttlError.isEmpty() && ttl.type == RespType::Integer
                    ? ttl.integer
                    : -1;
                guard->sendCommand({QByteArrayLiteral("RESTORE"), info.key,
                                    QByteArray::number(ttlMs > 0 ? ttlMs : 0), dump.scalar,
                                    QByteArrayLiteral("REPLACE")},
                                   [this, guard, info](const RespValue &restore,
                                                       const QString &restoreError) {
                    if (!guard)
                        return;
                    if (!restoreError.isEmpty() || restore.isError()) {
                        m_scanStatus->setText(
                            tr("Copy failed: %1")
                                .arg(restoreError.isEmpty()
                                         ? QString::fromUtf8(restore.scalar)
                                         : restoreError));
                    } else {
                        m_scanStatus->setText(tr("Copied %1").arg(QString::fromUtf8(info.key)));
                    }
                    guard->disconnectFromHost();
                    guard->deleteLater();
                });
            });
        });
    });
    m_scanStatus->setText(tr("Copying key..."));
    target->setTlsConfig(tlsConfigForProfile(targetProfile));
    if (!targetProfile.unixSocketPath.isEmpty()) {
        target->connectToUnixSocket(targetProfile.unixSocketPath, targetProfile.username, password,
                                    targetProfile.db);
    } else {
        target->connectToHost(targetProfile.host, targetProfile.port, targetProfile.tls,
                              targetProfile.username, password, targetProfile.db);
    }
}

void MainWindow::applyNamespaceFilter(const QString &path)
{
    if (!m_router.isReady())
        return;
    m_filterEdit->setText(path + m_treeModel->separator() + QStringLiteral("*"));
    m_viewMode->setCurrentIndex(1);
    startScan();
}

void MainWindow::sendCommand()
{
    const QString line = m_commandEdit->text().trimmed();
    if (line.isEmpty())
        return;
    if (!m_router.isReady()) {
        appendOutput(tr("[error] Not connected"));
        return;
    }

    QList<QByteArray> args;
    QString error;
    if (!CommandEncoder::parseLine(line, &args, &error)) {
        appendOutput(QStringLiteral("[error] ") + error);
        return;
    }

    const CommandDanger danger = CommandSafety::classify(args);
    if (danger != CommandDanger::Safe) {
        const QString text = danger == CommandDanger::Destructive
            ? tr("This command can destroy data or change server state:\n\n%1\n\nExecute?")
                  .arg(line)
            : tr("This command may block or alter the server:\n\n%1\n\nExecute?").arg(line);
        QMessageBox box(QMessageBox::Warning, tr("Confirm command"), text,
                        QMessageBox::Yes | QMessageBox::No, this);
        box.setDefaultButton(QMessageBox::No);
        if (box.exec() != QMessageBox::Yes)
            return;
    }

    appendOutput(QStringLiteral("> ") + line);
    m_router.sendCommand(args);
    m_commandEdit->clear();
}

void MainWindow::appendOutput(const QString &text)
{
    m_output->appendPlainText(text);
    m_output->verticalScrollBar()->setValue(m_output->verticalScrollBar()->maximum());
}

void MainWindow::setConnectedUi(bool connected)
{
    const bool readOnly = m_readOnlyCheck && m_readOnlyCheck->isChecked();
    m_sendButton->setEnabled(connected && !readOnly);
    m_commandEdit->setEnabled(connected && !readOnly);
    m_scanButton->setEnabled(connected);
    m_indexAllButton->setEnabled(connected && !m_indexingAll);
    m_filterEdit->setEnabled(connected);
    m_typeCombo->setEnabled(connected);
    const bool cluster = m_clusterCheck && m_clusterCheck->isChecked();
    m_dbSpin->setEnabled(connected && !cluster);
    m_dbCombo->setEnabled(connected && !cluster);
    m_clusterCheck->setEnabled(!connected);
    m_viewMode->setEnabled(connected);
    if (m_valuePanel)
        m_valuePanel->setReadOnly(readOnly);
    if (!connected) {
        m_scanner->stop();
        m_loadMoreButton->setEnabled(false);
    }
    updateProfileButtons();
    if (connected) {
        m_connectButton->setText(tr("Disconnect"));
        m_connectButton->setEnabled(true);
        m_profileConnectButton->setText(tr("Disconnect"));
    } else {
        m_connectButton->setText(tr("Connect"));
        m_connectButton->setEnabled(true);
        m_profileConnectButton->setText(tr("Connect"));
    }
}

} // namespace KeyDis
