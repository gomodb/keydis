// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QMainWindow>

class QSplitter;
class QAction;
class QTabWidget;

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
    QTabWidget *tabsForWidget(const MainWindow *session) const;
    void closeSession(QTabWidget *tabs, int index);
    void updateActiveTabs(QTabWidget *tabs);
    [[nodiscard]] QTabWidget *targetTabs() const;

    QSplitter *m_splitter = nullptr;
    QTabWidget *m_primaryTabs = nullptr;
    QTabWidget *m_secondaryTabs = nullptr;
    QTabWidget *m_activeTabs = nullptr;
    QAction *m_connectionsAction = nullptr;
    int m_sessionCounter = 0;
};

} // namespace KeyDis
