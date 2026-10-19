// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QByteArray>
#include <QDialog>

class QComboBox;
class QLabel;
class QPlainTextEdit;

namespace KeyDis {

class CompressionViewDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CompressionViewDialog(const QString &title, const QByteArray &bytes,
                                   QWidget *parent = nullptr);

private:
    void refresh();

    QByteArray m_bytes;
    QComboBox *m_format = nullptr;
    QPlainTextEdit *m_view = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace KeyDis
