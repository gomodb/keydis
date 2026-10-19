// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QByteArray>
#include <QDialog>

class QPlainTextEdit;
class QLabel;

namespace KeyDis {

class MsgPackViewDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MsgPackViewDialog(const QString &title, const QByteArray &bytes,
                               QWidget *parent = nullptr);

private:
    QPlainTextEdit *m_view = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace KeyDis
