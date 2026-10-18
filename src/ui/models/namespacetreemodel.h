// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include "core/keyspace/keyinfo.h"

#include <QAbstractItemModel>
#include <QByteArray>
#include <QSet>
#include <QString>
#include <QVector>

#include <memory>
#include <vector>

namespace KeyDis {

class NamespaceTreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Column {
        NameColumn = 0,
        TypeColumn,
        TtlColumn,
        ColumnCount
    };

    enum Role {
        KeyRole = Qt::UserRole + 1,
        KindRole,
        PathRole
    };

    explicit NamespaceTreeModel(QObject *parent = nullptr);
    ~NamespaceTreeModel() override;

    [[nodiscard]] QModelIndex index(int row, int column,
                                    const QModelIndex &parent = {}) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex &index) const override;
    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex &index) const override;

    void setSeparator(const QString &separator);
    [[nodiscard]] QString separator() const;
    void clear();
    void addKeys(const QVector<KeyInfo> &keys);
    void removeKey(const QByteArray &key);
    void updateKey(const KeyInfo &info);

    [[nodiscard]] bool isKey(const QModelIndex &index) const;
    [[nodiscard]] KeyInfo keyInfo(const QModelIndex &index) const;
    [[nodiscard]] int loadedKeyCount() const;

private:
    struct Node {
        Node *parent = nullptr;
        std::vector<std::unique_ptr<Node>> children;
        QString segment;
        QString path;
        QByteArray fullKey;
        KeyInfo info;
        int count = 0;
        bool isKey = false;
    };

    static bool lessThan(const Node &left, const Node &right);
    [[nodiscard]] int rowOf(const Node *node) const;
    [[nodiscard]] QModelIndex indexForNode(const Node *node) const;
    Node *ensureNamespace(Node *parent, const QString &segment, const QString &path, bool notify);
    void insertNode(Node *parent, std::unique_ptr<Node> node, bool notify);
    void insertKey(const KeyInfo &info, bool notify);
    void rebuild();
    Node *findKeyNode(Node *node, const QByteArray &key) const;

    std::unique_ptr<Node> m_root;
    QVector<KeyInfo> m_keys;
    QSet<QByteArray> m_keySet;
    QString m_separator = QStringLiteral(":");
};

} // namespace KeyDis
