// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "app/sessionhost.h"

#include "app/mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QKeySequence>
#include <QMenuBar>
#include <QSplitter>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>

namespace KeyDis {

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
    auto *newTabAction = fileMenu->addAction(tr("New Tab"));
    newTabAction->setObjectName(QStringLiteral("newSessionAction"));
    newTabAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+T")));
    connect(newTabAction, &QAction::triggered, this, [this] { addSession(); });

    auto *closeTabAction = fileMenu->addAction(tr("Close Tab"));
    closeTabAction->setShortcut(QKeySequence::Close);
    connect(closeTabAction, &QAction::triggered, this, [this] {
        QTabWidget *tabs = targetTabs();
        closeSession(tabs, tabs->currentIndex());
    });

    fileMenu->addSeparator();
    auto *quitAction = fileMenu->addAction(tr("Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

    auto *viewMenu = menuBar()->addMenu(tr("&View"));
    auto *splitAction = viewMenu->addAction(tr("Split Right"));
    splitAction->setObjectName(QStringLiteral("splitViewAction"));
    splitAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+T")));
    connect(splitAction, &QAction::triggered, this, &SessionHost::splitView);
    auto *closeSplitAction = viewMenu->addAction(tr("Close Split"));
    closeSplitAction->setObjectName(QStringLiteral("closeSplitAction"));
    connect(closeSplitAction, &QAction::triggered, this, &SessionHost::closeSplit);

    viewMenu->addSeparator();
    m_connectionsAction = viewMenu->addAction(tr("Connections Dock"));
    m_connectionsAction->setObjectName(QStringLiteral("connectionsDockAction"));
    m_connectionsAction->setCheckable(true);
    m_connectionsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+B")));
    connect(m_connectionsAction, &QAction::triggered, this, [this](bool checked) {
        if (auto *session = qobject_cast<MainWindow *>(targetTabs()->currentWidget()))
            session->setConnectionsDockVisible(checked);
        updateConnectionsAction();
    });
    auto *viewToolBar = addToolBar(tr("View"));
    viewToolBar->setObjectName(QStringLiteral("viewToolBar"));
    viewToolBar->addAction(m_connectionsAction);
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
    connect(session, &MainWindow::connectionsDockVisibilityChanged, this,
            [this, session](bool) {
        if (qobject_cast<MainWindow *>(targetTabs()->currentWidget()) == session)
            updateConnectionsAction();
    });

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

void SessionHost::updateConnectionsAction()
{
    if (!m_connectionsAction)
        return;
    auto *session = qobject_cast<MainWindow *>(targetTabs()->currentWidget());
    m_connectionsAction->setChecked(session && session->connectionsDockVisible());
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
