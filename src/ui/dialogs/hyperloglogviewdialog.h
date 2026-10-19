// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QByteArray>
#include <QDialog>

class QLabel;

namespace KeyDis {

class RedisClient;

class HyperLogLogViewDialog : public QDialog
{
    Q_OBJECT

public:
    explicit HyperLogLogViewDialog(RedisClient *connection, const QByteArray &key,
                                   QWidget *parent = nullptr);

private:
    void refresh();

    RedisClient *m_connection = nullptr;
    QByteArray m_key;
    QLabel *m_status = nullptr;
};

} // namespace KeyDis
