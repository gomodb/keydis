// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QMainWindow>

class QSplitter;
class QAction;
class QMenu;
class QTabWidget;
class QToolBar;

namespace KeyDis {

class MainWindow;

class SessionHost : public QMainWindow
{
    Q_OBJECT

public:
    explicit SessionHost(QWidget *parent = nullptr);

    MainWindow *addSession(const QString &profileId = {});
    [[nodiscard]] MainWindow *firstSession() const;
    [[nodiscard]] int sessionCount() const;

public slots:
    void splitView();
    void closeSplit();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    MainWindow *addSession(const QString &profileId, QTabWidget *tabs);
    QTabWidget *createTabs();
    void updateConnectionsAction();
    void updateSessionActions();
    void toggleFullScreen(bool fullScreen);
    [[nodiscard]] MainWindow *currentSession() const;
    QTabWidget *tabsForWidget(const MainWindow *session) const;
    void closeSession(QTabWidget *tabs, int index);
    void updateActiveTabs(QTabWidget *tabs);
    [[nodiscard]] QTabWidget *targetTabs() const;

    QSplitter *m_splitter = nullptr;
    QTabWidget *m_primaryTabs = nullptr;
    QTabWidget *m_secondaryTabs = nullptr;
    QTabWidget *m_activeTabs = nullptr;
    QAction *m_connectionsAction = nullptr;
    QAction *m_connectionsSeparator = nullptr;
    QMenu *m_viewMenu = nullptr;
    QToolBar *m_mainToolBar = nullptr;
    QToolBar *m_dataToolBar = nullptr;
    QToolBar *m_viewToolBar = nullptr;
    QAction *m_newTabAction = nullptr;
    QAction *m_closeTabAction = nullptr;
    QAction *m_toggleConnectionAction = nullptr;
    QAction *m_refreshAction = nullptr;
    QAction *m_readOnlyAction = nullptr;
    QAction *m_splitAction = nullptr;
    QAction *m_closeSplitAction = nullptr;
    QAction *m_fullScreenAction = nullptr;
    QAction *m_renameAction = nullptr;
    QAction *m_deleteAction = nullptr;
    QAction *m_ttlAction = nullptr;
    int m_sessionCounter = 0;
};

} // namespace KeyDis
