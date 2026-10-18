// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "ui/models/namespacetreemodel.h"

#include <QIcon>

#include <algorithm>
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

NamespaceTreeModel::NamespaceTreeModel(QObject *parent)
    : QAbstractItemModel(parent)
    , m_root(std::make_unique<Node>())
{
}

NamespaceTreeModel::~NamespaceTreeModel() = default;

QModelIndex NamespaceTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return {};
    const Node *parentNode = parent.isValid() ? static_cast<Node *>(parent.internalPointer())
                                              : m_root.get();
    if (!parentNode || row < 0 || row >= static_cast<int>(parentNode->children.size()))
        return {};
    return createIndex(row, column, parentNode->children[static_cast<size_t>(row)].get());
}

QModelIndex NamespaceTreeModel::parent(const QModelIndex &index) const
{
    if (!index.isValid())
        return {};
    const Node *node = static_cast<Node *>(index.internalPointer());
    if (!node || !node->parent || node->parent == m_root.get())
        return {};
    return indexForNode(node->parent);
}

int NamespaceTreeModel::rowCount(const QModelIndex &parent) const
{
    if (parent.column() > 0)
        return 0;
    const Node *node = parent.isValid() ? static_cast<Node *>(parent.internalPointer())
                                        : m_root.get();
    return node ? static_cast<int>(node->children.size()) : 0;
}

int NamespaceTreeModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return ColumnCount;
}

QVariant NamespaceTreeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};
    const Node *node = static_cast<Node *>(index.internalPointer());
    if (!node)
        return {};

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case NameColumn:
            return node->segment;
        case TypeColumn:
            return node->isKey ? node->info.type : QString::number(node->count);
        case TtlColumn:
            return node->isKey ? node->info.ttlText() : QString();
        default:
            return {};
        }
    case Qt::DecorationRole:
        if (index.column() == NameColumn && !node->isKey)
            return QIcon::fromTheme(QStringLiteral("folder"));
        return {};
    case Qt::ToolTipRole:
        if (node->isKey)
            return displayKey(node->fullKey);
        return node->path.isEmpty() ? tr("Root") : node->path;
    case Qt::TextAlignmentRole:
        if (index.column() == TypeColumn)
            return int(Qt::AlignRight | Qt::AlignVCenter);
        return {};
    case KeyRole:
        return node->isKey ? QVariant(node->fullKey) : QVariant();
    case KindRole:
        return node->isKey;
    case PathRole:
        return node->path;
    default:
        return {};
    }
}

QVariant NamespaceTreeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case NameColumn:
        return tr("Name");
    case TypeColumn:
        return tr("Type / Count");
    case TtlColumn:
        return tr("TTL");
    default:
        return {};
    }
}

Qt::ItemFlags NamespaceTreeModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void NamespaceTreeModel::setSeparator(const QString &separator)
{
    if (m_separator == separator)
        return;
    m_separator = separator;
    rebuild();
}

QString NamespaceTreeModel::separator() const
{
    return m_separator;
}

void NamespaceTreeModel::clear()
{
    beginResetModel();
    m_root = std::make_unique<Node>();
    m_keys.clear();
    m_keySet.clear();
    endResetModel();
}

void NamespaceTreeModel::addKeys(const QVector<KeyInfo> &keys)
{
    for (const KeyInfo &info : keys) {
        if (!info.isValid() || m_keySet.contains(info.key))
            continue;
        m_keys.append(info);
        m_keySet.insert(info.key);
        insertKey(info, true);
    }
}

void NamespaceTreeModel::removeKey(const QByteArray &key)
{
    if (!m_keySet.contains(key))
        return;
    m_keySet.remove(key);
    for (int index = 0; index < m_keys.size(); ++index) {
        if (m_keys.at(index).key == key) {
            m_keys.removeAt(index);
            break;
        }
    }
    rebuild();
}

void NamespaceTreeModel::updateKey(const KeyInfo &info)
{
    for (KeyInfo &stored : m_keys) {
        if (stored.key == info.key) {
            stored = info;
            break;
        }
    }
    Node *node = findKeyNode(m_root.get(), info.key);
    if (!node)
        return;
    node->info = info;
    const QModelIndex keyIndex = indexForNode(node);
    if (keyIndex.isValid())
        emit dataChanged(keyIndex, keyIndex.siblingAtColumn(ColumnCount - 1));
}

bool NamespaceTreeModel::isKey(const QModelIndex &index) const
{
    if (!index.isValid())
        return false;
    const Node *node = static_cast<Node *>(index.internalPointer());
    return node && node->isKey;
}

KeyInfo NamespaceTreeModel::keyInfo(const QModelIndex &index) const
{
    if (!isKey(index))
        return {};
    return static_cast<Node *>(index.internalPointer())->info;
}

int NamespaceTreeModel::loadedKeyCount() const
{
    return m_keys.size();
}

bool NamespaceTreeModel::lessThan(const Node &left, const Node &right)
{
    if (left.isKey != right.isKey)
        return !left.isKey;
    const int comparison = QString::compare(left.segment, right.segment, Qt::CaseInsensitive);
    if (comparison != 0)
        return comparison < 0;
    return left.segment < right.segment;
}

int NamespaceTreeModel::rowOf(const Node *node) const
{
    if (!node || !node->parent)
        return -1;
    const auto &siblings = node->parent->children;
    for (size_t index = 0; index < siblings.size(); ++index) {
        if (siblings[index].get() == node)
            return static_cast<int>(index);
    }
    return -1;
}

QModelIndex NamespaceTreeModel::indexForNode(const Node *node) const
{
    if (!node || node == m_root.get() || !node->parent)
        return {};
    return createIndex(rowOf(node), 0, const_cast<Node *>(node));
}

NamespaceTreeModel::Node *NamespaceTreeModel::ensureNamespace(Node *parent, const QString &segment,
                                                              const QString &path, bool notify)
{
    for (const auto &child : parent->children) {
        if (!child->isKey && child->segment == segment)
            return child.get();
    }

    auto node = std::make_unique<Node>();
    node->parent = parent;
    node->segment = segment;
    node->path = path;
    Node *raw = node.get();
    insertNode(parent, std::move(node), notify);
    return raw;
}

void NamespaceTreeModel::insertNode(Node *parent, std::unique_ptr<Node> node, bool notify)
{
    auto position = std::lower_bound(
        parent->children.begin(), parent->children.end(), node,
        [](const std::unique_ptr<Node> &left, const std::unique_ptr<Node> &right) {
            return lessThan(*left, *right);
        });
    const int row = static_cast<int>(std::distance(parent->children.begin(), position));
    if (notify) {
        const QModelIndex parentIndex = indexForNode(parent);
        beginInsertRows(parentIndex, row, row);
    }
    parent->children.insert(position, std::move(node));
    if (notify)
        endInsertRows();
}

void NamespaceTreeModel::insertKey(const KeyInfo &info, bool notify)
{
    const QString keyText = displayKey(info.key);
    const QStringList parts =
        m_separator.isEmpty() ? QStringList{keyText} : keyText.split(m_separator, Qt::KeepEmptyParts);

    if (parts.size() <= 1) {
        auto keyNode = std::make_unique<Node>();
        keyNode->parent = m_root.get();
        keyNode->segment = keyText;
        keyNode->path = keyText;
        keyNode->fullKey = info.key;
        keyNode->info = info;
        keyNode->count = 1;
        keyNode->isKey = true;
        insertNode(m_root.get(), std::move(keyNode), notify);
        return;
    }

    Node *parent = m_root.get();
    QString path;
    for (int index = 0; index < parts.size() - 1; ++index) {
        path = path.isEmpty() ? parts.at(index) : path + m_separator + parts.at(index);
        Node *namespaceNode = ensureNamespace(parent, parts.at(index), path, notify);
        ++namespaceNode->count;
        if (notify) {
            const QModelIndex namespaceIndex = indexForNode(namespaceNode);
            if (namespaceIndex.isValid())
                emit dataChanged(namespaceIndex.siblingAtColumn(TypeColumn),
                                 namespaceIndex.siblingAtColumn(TypeColumn));
        }
        parent = namespaceNode;
    }

    auto keyNode = std::make_unique<Node>();
    keyNode->parent = parent;
    keyNode->segment = parts.constLast();
    keyNode->path = keyText;
    keyNode->fullKey = info.key;
    keyNode->info = info;
    keyNode->count = 1;
    keyNode->isKey = true;
    insertNode(parent, std::move(keyNode), notify);
}

void NamespaceTreeModel::rebuild()
{
    beginResetModel();
    m_root = std::make_unique<Node>();
    for (const KeyInfo &info : std::as_const(m_keys))
        insertKey(info, false);
    endResetModel();
}

NamespaceTreeModel::Node *NamespaceTreeModel::findKeyNode(Node *node, const QByteArray &key) const
{
    if (!node)
        return nullptr;
    if (node->isKey && node->fullKey == key)
        return node;
    for (auto &child : node->children) {
        if (Node *found = findKeyNode(child.get(), key))
            return found;
    }
    return nullptr;
}

} // namespace KeyDis
