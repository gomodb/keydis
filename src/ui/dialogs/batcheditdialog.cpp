// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "ui/dialogs/batcheditdialog.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace KeyDis {

BatchEditDialog::BatchEditDialog(const QString &title, const QStringList &headers,
                                 const QVector<QPair<QString, QString>> &rows, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(title);
    setModal(true);
    resize(520, 360);

    auto *layout = new QVBoxLayout(this);
    m_table = new QTableWidget(rows.size(), headers.size(), this);
    m_table->setObjectName(QStringLiteral("batchEditTable"));
    m_table->setHorizontalHeaderLabels(headers);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked
                             | QAbstractItemView::SelectedClicked
                             | QAbstractItemView::EditKeyPressed);

    for (int row = 0; row < rows.size(); ++row) {
        auto *name = new QTableWidgetItem(rows.at(row).first);
        name->setFlags(name->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, 0, name);
        m_table->setItem(row, 1, new QTableWidgetItem(rows.at(row).second));
    }
    layout->addWidget(m_table, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QVector<QPair<QString, QString>> BatchEditDialog::values() const
{
    QVector<QPair<QString, QString>> result;
    result.reserve(m_table->rowCount());
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QTableWidgetItem *name = m_table->item(row, 0);
        QTableWidgetItem *value = m_table->item(row, 1);
        if (name && value)
            result.append({name->text(), value->text()});
    }
    return result;
}

} // namespace KeyDis
