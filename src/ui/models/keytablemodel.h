// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include "core/keyspace/keyinfo.h"

#include <QAbstractTableModel>
#include <QByteArray>
#include <QVector>

namespace KeyDis {

class KeyTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        KeyColumn = 0,
        TypeColumn,
        TtlColumn,
        ColumnCount
    };

    enum Role {
        KeyRole = Qt::UserRole + 1,
        TypeRole,
        TtlRole
    };

    explicit KeyTableModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex &index) const override;

    void setKeys(const QVector<KeyInfo> &keys);
    void appendKeys(const QVector<KeyInfo> &keys);
    void clear();
    void removeKey(const QByteArray &key);
    void updateKey(const KeyInfo &info);

    [[nodiscard]] KeyInfo keyAt(int row) const;
    [[nodiscard]] int indexOf(const QByteArray &key) const;
    [[nodiscard]] int count() const;

private:
    QVector<KeyInfo> m_keys;
};

} // namespace KeyDis
