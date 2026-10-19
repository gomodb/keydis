// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include "core/net/redisclient.h"

#include "core/keyspace/keyinfo.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QVector>

#include <functional>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QTableWidget;

namespace KeyDis {

struct RespValue;

class ValuePanel : public QWidget
{
    Q_OBJECT

public:
    explicit ValuePanel(RedisClient *connection, QWidget *parent = nullptr);

    void loadKey(const KeyInfo &info);
    [[nodiscard]] QByteArray currentKey() const;
    void clearKey();
    void setReadOnly(bool readOnly);
    [[nodiscard]] bool isReadOnly() const;

signals:
    void keyChanged(const QByteArray &key);
    void keyDeleted(const QByteArray &key);

private:
    enum Page {
        EmptyPage = 0,
        StringPage,
        HashPage,
        ListPage,
        SetPage,
        ZSetPage,
        StreamPage,
        JsonPage,
        RawPage
    };

    void buildUi();
    QWidget *buildStringPage();
    QWidget *buildHashPage();
    QWidget *buildListPage();
    QWidget *buildSetPage();
    QWidget *buildZSetPage();
    QWidget *buildStreamPage();
    QWidget *buildJsonPage();
    QWidget *buildRawPage();

    void loadString();
    void loadFullString();
    void applyStringBytes(const QByteArray &bytes, bool partial, qint64 totalSize);
    void updateStringControls();
    void loadHash();
    void loadHashPage(const QByteArray &cursor, bool append);
    void refreshHashFieldTtl();
    void editHashFieldTtl();
    void loadList();
    void loadListPage(int offset, bool append);
    void loadSet();
    void loadSetPage(const QByteArray &cursor, bool append);
    void loadZSet();
    void loadZSetPage(const QByteArray &cursor, bool append);
    void loadStream();
    void loadStreamGroups();
    void createStreamGroup();
    void deleteStreamGroup();
    void loadPending();
    void claimStreamEntries();
    void ackStreamEntries();
    void addStreamEntry();
    void deleteStreamEntries();
    void loadJson();
    void saveJson();
    void rebuildJsonTree();
    void handleJsonTreeEdited(QTreeWidgetItem *item, int column);
    void formatJson();
    void showHexView();
    void showDecodedValue();
    void showMsgPackValue();
    void showBitmapView();
    void showHyperLogLogView();
    void editStringBytes();
    void stringTextChanged();
    void addJsonChild();
    void deleteJsonNode();
    void updateJsonButtons();
    void filterValueRows(const QString &text);
    void searchAllValues();
    void renameJsonKey();
    void loadRaw(const QString &type);

    void updateHeader();
    void editTtl();
    void deleteKey();
    void saveString();
    void partialStringEdit();
    void editHashSelected();
    void addHashField();
    void deleteHashFields();
    void editListSelected();
    void editSetSelected();
    void editZSetSelected();
    void addListValue(bool head);
    void deleteListValues();
    void addSetMember();
    void deleteSetMembers();
    void addZSetMember();
    void deleteZSetMembers();

    void setStatus(const QString &text);
    void showError(const QString &error);
    void runCommand(const QList<QByteArray> &args,
                    std::function<void(const RespValue &, const QString &)> callback);
    void runCommands(const QList<QList<QByteArray>> &commands, std::function<void(bool)> done);
    void pushUndo(const QString &label, const QList<QList<QByteArray>> &undoCommands,
                  const QList<QList<QByteArray>> &redoCommands = {});
    void undoLast();
    void redoLast();
    void updateDirtyLabel();
    void discardString();
    void discardJson();
    void stageCommands(const QString &label, const QList<QList<QByteArray>> &commands,
                       const QList<QList<QByteArray>> &undoCommands,
                       const QList<QList<QByteArray>> &redoCommands);
    void applyPending();
    void discardPending();
    void updatePendingButtons();
    void clearPending();
    void clearUndo();
    void updateUndoButton();

    RedisClient *m_connection = nullptr;
    QByteArray m_key;
    QString m_type;
    qint64 m_ttlMs = -2;
    quint64 m_generation = 0;
    bool m_loading = false;
    bool m_readOnly = false;

    QLabel *m_keyLabel = nullptr;
    QLabel *m_typeLabel = nullptr;
    QLabel *m_ttlLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_dirtyLabel = nullptr;
    QStackedWidget *m_stack = nullptr;
    QPlainTextEdit *m_stringEdit = nullptr;
    QPushButton *m_stringDiscardButton = nullptr;
    QPushButton *m_stringLoadFullButton = nullptr;
    QPushButton *m_bitmapButton = nullptr;
    QPushButton *m_hllButton = nullptr;
    QList<QPushButton *> m_stringWriteButtons;
    bool m_stringPartial = false;
    qint64 m_stringTotalSize = 0;
    QByteArray m_stringRaw;
    QByteArray m_stringLoadedRaw;
    QString m_stringLoaded;
    bool m_stringUpdating = false;
    bool m_stringDirty = false;
    QPlainTextEdit *m_jsonEdit = nullptr;
    QPushButton *m_jsonDiscardButton = nullptr;
    QString m_jsonLoaded;
    bool m_jsonDirty = false;
    QLineEdit *m_jsonPathEdit = nullptr;
    QLineEdit *m_valueFilter = nullptr;
    QPushButton *m_searchAllButton = nullptr;
    QTreeWidget *m_jsonTree = nullptr;
    QPushButton *m_jsonAddButton = nullptr;
    QPushButton *m_jsonDeleteButton = nullptr;
    QPushButton *m_jsonRenameButton = nullptr;
    bool m_jsonTreeUpdating = false;
    QPlainTextEdit *m_rawView = nullptr;
    QTableWidget *m_hashTable = nullptr;
    QTableWidget *m_listTable = nullptr;
    QTableWidget *m_setTable = nullptr;
    QTableWidget *m_zsetTable = nullptr;
    QTableWidget *m_streamTable = nullptr;
    QTableWidget *m_streamGroupsTable = nullptr;
    QTableWidget *m_streamPendingTable = nullptr;
    QPushButton *m_undoButton = nullptr;
    QPushButton *m_redoButton = nullptr;
    QPushButton *m_applyButton = nullptr;
    QPushButton *m_discardButton = nullptr;
    struct UndoAction {
        QString label;
        QList<QList<QByteArray>> undoCommands;
        QList<QList<QByteArray>> redoCommands;
    };
    QVector<UndoAction> m_undoActions;
    QVector<UndoAction> m_redoActions;
    QList<QList<QByteArray>> m_pendingCommands;
    QList<QList<QByteArray>> m_pendingUndo;
    QList<QList<QByteArray>> m_pendingRedo;
    QString m_pendingLabel;
    QPushButton *m_hashMoreButton = nullptr;
    QPushButton *m_hashTtlButton = nullptr;
    bool m_hashTtlSupported = true;
    QPushButton *m_listMoreButton = nullptr;
    QPushButton *m_setMoreButton = nullptr;
    QPushButton *m_zsetMoreButton = nullptr;
    QHash<QString, QString> m_hashOriginal;
    QHash<int, QString> m_listOriginal;
    QHash<QString, QString> m_zsetOriginal;
    QByteArray m_hashCursor = QByteArrayLiteral("0");
    QByteArray m_setCursor = QByteArrayLiteral("0");
    QByteArray m_zsetCursor = QByteArrayLiteral("0");
    int m_listOffset = 0;
    QList<QWidget *> m_writeWidgets;
    QList<QTableWidget *> m_valueTables;
};

} // namespace KeyDis
