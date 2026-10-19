// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QMainWindow>

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

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void closeSession(int index);

    QTabWidget *m_tabs = nullptr;
    int m_sessionCounter = 0;
};

} // namespace KeyDis
