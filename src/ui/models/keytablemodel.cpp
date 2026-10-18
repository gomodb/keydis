// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "ui/models/keytablemodel.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QSet>

#include <utility>

namespace KeyDis {

namespace {

QString displayKey(const QByteArray &key)
{
    const QString text = QString::fromUtf8(key);
    if (key.contains('\0') || text.contains(QChar::ReplacementCharacter))
        return QStringLiteral("0x") + QString::fromLatin1(key.toHex());
    return text;
}

} // namespace

KeyTableModel::KeyTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int KeyTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_keys.size();
}

int KeyTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant KeyTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_keys.size())
        return {};

    const KeyInfo &info = m_keys.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case KeyColumn:
            return displayKey(info.key);
        case TypeColumn:
            return info.type;
        case TtlColumn:
            return info.ttlText();
        default:
            return {};
        }
    case Qt::ToolTipRole:
        return displayKey(info.key);
    case KeyRole:
        return info.key;
    case TypeRole:
        return info.type;
    case TtlRole:
        return info.ttlMs;
    case Qt::ForegroundRole:
        if (info.ttlMs > 0 && info.ttlMs < 60 * 1000)
            return QBrush(QColor(QStringLiteral("#da4453")));
        return {};
    default:
        return {};
    }
}

QVariant KeyTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case KeyColumn:
        return tr("Key");
    case TypeColumn:
        return tr("Type");
    case TtlColumn:
        return tr("TTL");
    default:
        return {};
    }
}

Qt::ItemFlags KeyTableModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void KeyTableModel::setKeys(const QVector<KeyInfo> &keys)
{
    beginResetModel();
    m_keys = keys;
    endResetModel();
}

void KeyTableModel::appendKeys(const QVector<KeyInfo> &keys)
{
    if (keys.isEmpty())
        return;

    QSet<QByteArray> existing;
    existing.reserve(m_keys.size());
    for (const KeyInfo &info : std::as_const(m_keys))
        existing.insert(info.key);

    QVector<KeyInfo> fresh;
    fresh.reserve(keys.size());
    for (const KeyInfo &info : keys) {
        if (existing.contains(info.key))
            continue;
        existing.insert(info.key);
        fresh.append(info);
    }
    if (fresh.isEmpty())
        return;

    const int first = m_keys.size();
    beginInsertRows({}, first, first + fresh.size() - 1);
    m_keys += fresh;
    endInsertRows();
}

void KeyTableModel::clear()
{
    if (m_keys.isEmpty())
        return;
    beginResetModel();
    m_keys.clear();
    endResetModel();
}

void KeyTableModel::removeKey(const QByteArray &key)
{
    const int row = indexOf(key);
    if (row < 0)
        return;
    beginRemoveRows({}, row, row);
    m_keys.removeAt(row);
    endRemoveRows();
}

void KeyTableModel::updateKey(const KeyInfo &info)
{
    const int row = indexOf(info.key);
    if (row < 0)
        return;
    m_keys[row] = info;
    emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
}

KeyInfo KeyTableModel::keyAt(int row) const
{
    if (row < 0 || row >= m_keys.size())
        return {};
    return m_keys.at(row);
}

int KeyTableModel::indexOf(const QByteArray &key) const
{
    for (int index = 0; index < m_keys.size(); ++index) {
        if (m_keys.at(index).key == key)
            return index;
    }
    return -1;
}

int KeyTableModel::count() const
{
    return m_keys.size();
}

} // namespace KeyDis
