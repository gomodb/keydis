// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "app/sessionhost.h"

#include "app/mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QIcon>
#include <QKeySequence>
#include <QMenu>
#include <QMessageBox>
#include <QStyle>
#include <QMenuBar>
#include <QSplitter>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>

namespace KeyDis {

namespace {
QIcon themeIcon(QWidget *widget, const QString &name, QStyle::StandardPixmap fallback)
{
    QIcon icon = QIcon::fromTheme(name);
    if (icon.isNull())
        icon = widget->style()->standardIcon(fallback);
    return icon;
}
} // namespace

SessionHost::SessionHost(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("KeyDis"));

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName(QStringLiteral("sessionSplitter"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(8);
    m_splitter->setOpaqueResize(true);
    setCentralWidget(m_splitter);

    m_primaryTabs = createTabs();
    m_primaryTabs->setObjectName(QStringLiteral("sessionTabs"));
    m_activeTabs = m_primaryTabs;
    m_splitter->addWidget(m_primaryTabs);
    m_splitter->setStretchFactor(0, 1);

    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    m_newTabAction = fileMenu->addAction(
        themeIcon(this, QStringLiteral("document-new"), QStyle::SP_FileIcon), tr("New Tab"));
    m_newTabAction->setObjectName(QStringLiteral("newSessionAction"));
    m_newTabAction->setShortcuts({QKeySequence::New, QKeySequence(QStringLiteral("Ctrl+T"))});
    connect(m_newTabAction, &QAction::triggered, this, [this] { addSession(); });

    m_closeTabAction = fileMenu->addAction(
        themeIcon(this, QStringLiteral("window-close"), QStyle::SP_DialogCloseButton),
        tr("Close Tab"));
    m_closeTabAction->setObjectName(QStringLiteral("closeSessionAction"));
    m_closeTabAction->setShortcut(QKeySequence::Close);
    connect(m_closeTabAction, &QAction::triggered, this, [this] {
        QTabWidget *tabs = targetTabs();
        closeSession(tabs, tabs->currentIndex());
    });

    fileMenu->addSeparator();
    auto *quitAction = fileMenu->addAction(
        themeIcon(this, QStringLiteral("application-exit"), QStyle::SP_DialogCloseButton),
        tr("Quit"));
    quitAction->setObjectName(QStringLiteral("quitAction"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

    m_viewMenu = menuBar()->addMenu(tr("&View"));
    m_splitAction = m_viewMenu->addAction(
        themeIcon(this, QStringLiteral("view-split-left-right"), QStyle::SP_FileDialogDetailedView),
        tr("Split Right"));
    m_splitAction->setObjectName(QStringLiteral("splitViewAction"));
    m_splitAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+T")));
    connect(m_splitAction, &QAction::triggered, this, &SessionHost::splitView);

    m_closeSplitAction = m_viewMenu->addAction(
        themeIcon(this, QStringLiteral("view-split-left-right"), QStyle::SP_FileDialogListView),
        tr("Close Split"));
    m_closeSplitAction->setObjectName(QStringLiteral("closeSplitAction"));
    connect(m_closeSplitAction, &QAction::triggered, this, &SessionHost::closeSplit);

    m_connectionsSeparator = m_viewMenu->addSeparator();
    m_viewMenu->addSeparator();
    m_fullScreenAction = m_viewMenu->addAction(
        themeIcon(this, QStringLiteral("view-fullscreen"), QStyle::SP_TitleBarMaxButton),
        tr("Full Screen"));
    m_fullScreenAction->setObjectName(QStringLiteral("fullScreenAction"));
    m_fullScreenAction->setCheckable(true);
    m_fullScreenAction->setShortcut(QKeySequence(Qt::Key_F11));
    connect(m_fullScreenAction, &QAction::toggled, this, &SessionHost::toggleFullScreen);

    auto *toolbarsMenu = m_viewMenu->addMenu(tr("Toolbars"));
    toolbarsMenu->setObjectName(QStringLiteral("toolbarsMenu"));

    auto *connectionMenu = menuBar()->addMenu(tr("&Connection"));
    m_toggleConnectionAction = connectionMenu->addAction(
        themeIcon(this, QStringLiteral("network-connect"), QStyle::SP_DialogApplyButton),
        tr("Connect / Disconnect"));
    m_toggleConnectionAction->setObjectName(QStringLiteral("toggleConnectionAction"));
    connect(m_toggleConnectionAction, &QAction::triggered, this, [this] {
        if (auto *session = currentSession())
            session->toggleConnection();
    });

    m_refreshAction = connectionMenu->addAction(
        themeIcon(this, QStringLiteral("view-refresh"), QStyle::SP_BrowserReload),
        tr("Refresh Keys"));
    m_refreshAction->setObjectName(QStringLiteral("refreshKeysAction"));
    m_refreshAction->setShortcut(QKeySequence(Qt::Key_F5));
    connect(m_refreshAction, &QAction::triggered, this, [this] {
        if (auto *session = currentSession())
            session->refreshKeys();
    });

    connectionMenu->addSeparator();
    m_readOnlyAction = connectionMenu->addAction(tr("Read-only Mode"));
    m_readOnlyAction->setObjectName(QStringLiteral("readOnlyModeAction"));
    m_readOnlyAction->setCheckable(true);
    connect(m_readOnlyAction, &QAction::toggled, this, [this](bool readOnly) {
        if (auto *session = currentSession())
            session->setReadOnlyMode(readOnly);
    });

    auto *dataMenu = menuBar()->addMenu(tr("&Data"));
    m_renameAction = dataMenu->addAction(
        themeIcon(this, QStringLiteral("edit-rename"), QStyle::SP_FileDialogDetailedView),
        tr("Rename Key"));
    m_renameAction->setObjectName(QStringLiteral("renameKeyAction"));
    m_renameAction->setShortcut(QKeySequence(Qt::Key_F2));
    connect(m_renameAction, &QAction::triggered, this, [this] {
        if (auto *session = currentSession())
            session->renameSelectedKey();
    });

    m_deleteAction = dataMenu->addAction(
        themeIcon(this, QStringLiteral("edit-delete"), QStyle::SP_TrashIcon),
        tr("Delete Selected Keys"));
    m_deleteAction->setObjectName(QStringLiteral("deleteKeysAction"));
    connect(m_deleteAction, &QAction::triggered, this, [this] {
        if (auto *session = currentSession())
            session->deleteSelectedKeys();
    });

    m_ttlAction = dataMenu->addAction(
        themeIcon(this, QStringLiteral("chronometer"), QStyle::SP_MessageBoxQuestion),
        tr("Set TTL"));
    m_ttlAction->setObjectName(QStringLiteral("setTtlAction"));
    connect(m_ttlAction, &QAction::triggered, this, [this] {
        if (auto *session = currentSession())
            session->setSelectedKeysTtl();
    });

    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    auto *aboutAction = helpMenu->addAction(
        themeIcon(this, QStringLiteral("help-about"), QStyle::SP_MessageBoxInformation),
        tr("About KeyDis"));
    aboutAction->setObjectName(QStringLiteral("aboutAction"));
    connect(aboutAction, &QAction::triggered, this, [this] {
        QMessageBox::about(
            this, tr("About KeyDis"),
            tr("<b>KeyDis</b> %1<br/>A native Redis/Valkey client for KDE.")
                .arg(QCoreApplication::applicationVersion()));
    });
    auto *aboutQtAction = helpMenu->addAction(tr("About Qt"));
    aboutQtAction->setObjectName(QStringLiteral("aboutQtAction"));
    connect(aboutQtAction, &QAction::triggered, qApp, &QApplication::aboutQt);

    m_mainToolBar = addToolBar(tr("Connection"));
    m_mainToolBar->setObjectName(QStringLiteral("toolBarConnection"));
    m_mainToolBar->setIconSize(QSize(20, 20));
    m_mainToolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_mainToolBar->addAction(m_newTabAction);
    m_mainToolBar->addAction(m_toggleConnectionAction);
    m_mainToolBar->addAction(m_refreshAction);
    m_mainToolBar->addAction(m_splitAction);

    m_dataToolBar = addToolBar(tr("Data"));
    m_dataToolBar->setObjectName(QStringLiteral("toolBarData"));
    m_dataToolBar->setIconSize(QSize(20, 20));
    m_dataToolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_dataToolBar->addAction(m_renameAction);
    m_dataToolBar->addAction(m_deleteAction);
    m_dataToolBar->addAction(m_ttlAction);

    m_viewToolBar = addToolBar(tr("View"));
    m_viewToolBar->setObjectName(QStringLiteral("toolBarView"));
    m_viewToolBar->setIconSize(QSize(20, 20));
    m_viewToolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_viewToolBar->addAction(m_fullScreenAction);

    toolbarsMenu->addAction(m_mainToolBar->toggleViewAction());
    toolbarsMenu->addAction(m_dataToolBar->toggleViewAction());
    toolbarsMenu->addAction(m_viewToolBar->toggleViewAction());

    updateSessionActions();
}

QTabWidget *SessionHost::createTabs()
{
    auto *tabs = new QTabWidget(m_splitter);
    tabs->setTabsClosable(true);
    tabs->setMovable(true);
    tabs->setDocumentMode(true);

    auto *newTabButton = new QToolButton(tabs);
    newTabButton->setObjectName(QStringLiteral("newSessionButton"));
    newTabButton->setText(QStringLiteral("+"));
    newTabButton->setToolTip(tr("New connection tab"));
    newTabButton->setAutoRaise(true);
    tabs->setCornerWidget(newTabButton, Qt::TopRightCorner);
    connect(newTabButton, &QToolButton::clicked, this,
            [this, tabs] { addSession({}, tabs); });

    connect(tabs, &QTabWidget::tabCloseRequested, this,
            [this, tabs](int index) { closeSession(tabs, index); });
    connect(tabs, &QTabWidget::currentChanged, this,
            [this, tabs](int) {
                updateActiveTabs(tabs);
                updateSessionActions();
                updateConnectionsAction();
            });
    return tabs;
}

MainWindow *SessionHost::addSession(const QString &profileId)
{
    return addSession(profileId, targetTabs());
}

MainWindow *SessionHost::addSession(const QString &profileId, QTabWidget *tabs)
{
    if (!tabs)
        tabs = m_primaryTabs;
    auto *session = new MainWindow(tabs);
    session->setWindowFlags(Qt::Widget);
    session->setAttribute(Qt::WA_DeleteOnClose, false);
    session->menuBar()->setVisible(false);

    const int index = tabs->addTab(session,
                                   tr("Connection %1").arg(++m_sessionCounter));
    tabs->setCurrentIndex(index);
    updateActiveTabs(tabs);
    session->show();

    connect(session, &MainWindow::sessionTitleChanged, this,
            [this, session](const QString &title) {
        QTabWidget *owner = tabsForWidget(session);
        if (!owner)
            return;
        const int current = owner->indexOf(session);
        if (current >= 0)
            owner->setTabText(current, title);
    });
    connect(session, &MainWindow::newSessionRequested, this,
            [this, tabs] { addSession({}, tabs); });
    connect(session, &MainWindow::newSessionWithProfileRequested, this,
            [this, tabs](const QString &id) { addSession(id, tabs); });
    connect(session, &MainWindow::readOnlyChanged, this, [this, session](bool) {
        if (currentSession() == session)
            updateSessionActions();
    });

    updateSessionActions();
    updateConnectionsAction();
    if (!profileId.isEmpty())
        session->connectProfileById(profileId);
    return session;
}

MainWindow *SessionHost::firstSession() const
{
    return m_primaryTabs && m_primaryTabs->count() > 0
        ? qobject_cast<MainWindow *>(m_primaryTabs->widget(0))
        : nullptr;
}

int SessionHost::sessionCount() const
{
    int count = m_primaryTabs ? m_primaryTabs->count() : 0;
    if (m_secondaryTabs)
        count += m_secondaryTabs->count();
    return count;
}

void SessionHost::splitView()
{
    if (!m_secondaryTabs) {
        m_secondaryTabs = createTabs();
        m_secondaryTabs->setObjectName(QStringLiteral("sessionTabsSecondary"));
        m_splitter->addWidget(m_secondaryTabs);
        m_splitter->setStretchFactor(1, 1);
        m_splitter->setSizes({1, 1});
    }
    addSession({}, m_secondaryTabs);
    updateActiveTabs(m_secondaryTabs);
}

void SessionHost::closeSplit()
{
    if (!m_secondaryTabs)
        return;
    QTabWidget *tabs = m_secondaryTabs;
    m_secondaryTabs = nullptr;
    m_activeTabs = m_primaryTabs;
    while (tabs->count() > 0) {
        QWidget *page = tabs->widget(0);
        tabs->removeTab(0);
        if (page)
            page->deleteLater();
    }
    tabs->deleteLater();
    updateSessionActions();
    updateConnectionsAction();
}

QTabWidget *SessionHost::tabsForWidget(const MainWindow *session) const
{
    if (m_primaryTabs && m_primaryTabs->indexOf(const_cast<MainWindow *>(session)) >= 0)
        return m_primaryTabs;
    if (m_secondaryTabs && m_secondaryTabs->indexOf(const_cast<MainWindow *>(session)) >= 0)
        return m_secondaryTabs;
    return nullptr;
}

QTabWidget *SessionHost::targetTabs() const
{
    return m_activeTabs ? m_activeTabs : m_primaryTabs;
}

void SessionHost::updateActiveTabs(QTabWidget *tabs)
{
    if (tabs && (tabs == m_primaryTabs || tabs == m_secondaryTabs))
        m_activeTabs = tabs;
}

MainWindow *SessionHost::currentSession() const
{
    return qobject_cast<MainWindow *>(targetTabs()->currentWidget());
}

void SessionHost::updateSessionActions()
{
    auto *session = currentSession();
    const bool hasSession = session != nullptr;
    for (QAction *action : {m_toggleConnectionAction, m_refreshAction, m_readOnlyAction,
                            m_renameAction, m_deleteAction, m_ttlAction, m_splitAction,
                            m_closeSplitAction}) {
        if (action)
            action->setEnabled(hasSession);
    }
    if (m_readOnlyAction) {
        const QSignalBlocker blocker(m_readOnlyAction);
        m_readOnlyAction->setChecked(hasSession && session->isReadOnly());
    }
    if (m_closeSplitAction)
        m_closeSplitAction->setEnabled(m_secondaryTabs != nullptr);
}

void SessionHost::updateConnectionsAction()
{
    auto *session = currentSession();
    QAction *next = session ? session->connectionsToggleAction() : nullptr;
    if (next == m_connectionsAction)
        return;
    if (m_connectionsAction) {
        m_connectionsAction->setShortcut(QKeySequence());
        m_connectionsAction->setObjectName(QStringLiteral("connectionsDockToggleAction"));
        m_viewMenu->removeAction(m_connectionsAction);
        m_viewToolBar->removeAction(m_connectionsAction);
    }
    m_connectionsAction = next;
    if (m_connectionsAction && m_connectionsSeparator) {
        m_connectionsAction->setObjectName(QStringLiteral("connectionsDockAction"));
        m_connectionsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+B")));
        m_viewMenu->insertAction(m_connectionsSeparator, m_connectionsAction);
        m_viewToolBar->insertAction(m_fullScreenAction, m_connectionsAction);
    }
}

void SessionHost::toggleFullScreen(bool fullScreen)
{
    if (fullScreen)
        showFullScreen();
    else
        showNormal();
}

void SessionHost::closeSession(QTabWidget *tabs, int index)
{
    if (!tabs || index < 0 || index >= tabs->count())
        return;
    QWidget *page = tabs->widget(index);
    tabs->removeTab(index);
    if (page)
        page->deleteLater();
    if (tabs == m_secondaryTabs && tabs->count() == 0) {
        closeSplit();
        return;
    }
    if (tabs == m_primaryTabs && tabs->count() == 0)
        close();
}

void SessionHost::closeEvent(QCloseEvent *event)
{
    QMainWindow::closeEvent(event);
    if (event->isAccepted())
        QCoreApplication::quit();
}

} // namespace KeyDis
