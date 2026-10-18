// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QDialog>
#include <QPair>
#include <QString>
#include <QVector>

class QTableWidget;

namespace KeyDis {

class BatchEditDialog : public QDialog
{
    Q_OBJECT

public:
    BatchEditDialog(const QString &title, const QStringList &headers,
                    const QVector<QPair<QString, QString>> &rows, QWidget *parent = nullptr);

    [[nodiscard]] QVector<QPair<QString, QString>> values() const;

private:
    QTableWidget *m_table = nullptr;
};

} // namespace KeyDis
