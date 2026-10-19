// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "app/sessionhost.h"

#include "app/mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QKeySequence>
#include <QMenuBar>
#include <QTabWidget>
#include <QToolButton>

namespace KeyDis {

SessionHost::SessionHost(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("KeyDis"));
    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("sessionTabs"));
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    setCentralWidget(m_tabs);

    auto *newTabButton = new QToolButton(m_tabs);
    newTabButton->setObjectName(QStringLiteral("newSessionButton"));
    newTabButton->setText(QStringLiteral("+"));
    newTabButton->setToolTip(tr("New connection tab"));
    newTabButton->setAutoRaise(true);
    m_tabs->setCornerWidget(newTabButton, Qt::TopRightCorner);
    connect(newTabButton, &QToolButton::clicked, this, [this] { addSession(); });

    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    auto *newTabAction = fileMenu->addAction(tr("New Tab"));
    newTabAction->setObjectName(QStringLiteral("newSessionAction"));
    newTabAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+T")));
    connect(newTabAction, &QAction::triggered, this, [this] { addSession(); });

    auto *closeTabAction = fileMenu->addAction(tr("Close Tab"));
    closeTabAction->setShortcut(QKeySequence::Close);
    connect(closeTabAction, &QAction::triggered, this, [this] {
        closeSession(m_tabs->currentIndex());
    });

    fileMenu->addSeparator();
    auto *quitAction = fileMenu->addAction(tr("Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &SessionHost::closeSession);
}

MainWindow *SessionHost::addSession(const QString &profileId)
{
    auto *session = new MainWindow(m_tabs);
    session->setWindowFlags(Qt::Widget);
    session->setAttribute(Qt::WA_DeleteOnClose, false);
    session->menuBar()->setVisible(false);

    const int index = m_tabs->addTab(session,
                                     tr("Connection %1").arg(++m_sessionCounter));
    m_tabs->setCurrentIndex(index);
    session->show();

    connect(session, &MainWindow::sessionTitleChanged, this,
            [this, session](const QString &title) {
        const int current = m_tabs->indexOf(session);
        if (current >= 0)
            m_tabs->setTabText(current, title);
    });
    connect(session, &MainWindow::newSessionRequested, this,
            [this] { addSession(); });
    connect(session, &MainWindow::newSessionWithProfileRequested, this,
            [this](const QString &id) { addSession(id); });

    if (!profileId.isEmpty())
        session->connectProfileById(profileId);
    return session;
}

void SessionHost::closeEvent(QCloseEvent *event)
{
    QMainWindow::closeEvent(event);
    if (event->isAccepted())
        QCoreApplication::quit();
}

MainWindow *SessionHost::firstSession() const
{
    return m_tabs && m_tabs->count() > 0
        ? qobject_cast<MainWindow *>(m_tabs->widget(0))
        : nullptr;
}

int SessionHost::sessionCount() const
{
    return m_tabs ? m_tabs->count() : 0;
}

void SessionHost::closeSession(int index)
{
    if (!m_tabs || index < 0 || index >= m_tabs->count())
        return;
    QWidget *page = m_tabs->widget(index);
    m_tabs->removeTab(index);
    if (page)
        page->deleteLater();
    if (m_tabs->count() == 0)
        close();
}

} // namespace KeyDis
