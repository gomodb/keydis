// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "ui/values/valuepanel.h"

#include "core/net/redisclient.h"
#include "core/resp/respvalue.h"
#include "ui/dialogs/batcheditdialog.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <memory>
#include <utility>

namespace KeyDis {

namespace {

QHash<QString, QString> responseMap(const RespValue &value)
{
    QHash<QString, QString> result;
    if (value.type == RespType::Map) {
        for (const auto &[key, item] : value.pairs)
            result.insert(QString::fromUtf8(key.scalar), QString::fromUtf8(item.scalar));
    } else if (value.type == RespType::Array) {
        for (size_t index = 0; index + 1 < value.elements.size(); index += 2) {
            result.insert(QString::fromUtf8(value.elements[index].scalar),
                          QString::fromUtf8(value.elements[index + 1].scalar));
        }
    }
    return result;
}


QString displayKey(const QByteArray &key)
{
    const QString text = QString::fromUtf8(key);
    if (key.contains('\0') || text.contains(QChar::ReplacementCharacter))
        return QStringLiteral("0x") + QString::fromLatin1(key.toHex());
    return text;
}

QString scalarText(const RespValue &value)
{
    if (value.type == RespType::Null)
        return {};
    return QString::fromUtf8(value.scalar);
}

QTableWidgetItem *readOnlyItem(const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

QVector<QPair<QString, QString>> selectedPairs(QTableWidget *table)
{
    QVector<QPair<QString, QString>> result;
    if (!table || !table->selectionModel())
        return result;
    const auto rows = table->selectionModel()->selectedRows();
    for (const QModelIndex &index : rows) {
        QTableWidgetItem *first = table->item(index.row(), 0);
        QTableWidgetItem *second = table->columnCount() > 1 ? table->item(index.row(), 1) : nullptr;
        if (first)
            result.append({first->text(), second ? second->text() : QString()});
    }
    return result;
}

QTableWidget *makeTable(const QStringList &headers, QWidget *parent)
{
    auto *table = new QTableWidget(0, headers.size(), parent);
    table->setHorizontalHeaderLabels(headers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked
                           | QAbstractItemView::EditKeyPressed);
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    table->setAlternatingRowColors(true);
    return table;
}

QWidget *tablePage(QTableWidget *table, const QList<QPushButton *> &buttons, QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(table, 1);
    auto *row = new QHBoxLayout;
    for (QPushButton *button : buttons)
        row->addWidget(button);
    row->addStretch();
    layout->addLayout(row);
    return page;
}

} // namespace

ValuePanel::ValuePanel(RedisClient *connection, QWidget *parent)
    : QWidget(parent)
    , m_connection(connection)
{
    buildUi();
    clearKey();
}

void ValuePanel::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);

    auto *header = new QHBoxLayout;
    m_keyLabel = new QLabel(this);
    m_keyLabel->setObjectName(QStringLiteral("keyLabel"));
    m_keyLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont keyFont = m_keyLabel->font();
    keyFont.setBold(true);
    m_keyLabel->setFont(keyFont);
    m_keyLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    header->addWidget(m_keyLabel, 1);

    m_typeLabel = new QLabel(this);
    m_typeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    header->addWidget(m_typeLabel);

    m_ttlLabel = new QLabel(this);
    m_ttlLabel->setMinimumWidth(150);
    m_ttlLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    header->addWidget(m_ttlLabel);

    auto *refreshButton = new QPushButton(tr("Refresh"), this);
    auto *ttlButton = new QPushButton(tr("TTL..."), this);
    auto *deleteButton = new QPushButton(tr("Delete"), this);
    header->addWidget(refreshButton);
    header->addWidget(ttlButton);
    header->addWidget(deleteButton);
    m_writeWidgets << ttlButton << deleteButton;
    layout->addLayout(header);

    m_stack = new QStackedWidget(this);
    m_stack->setObjectName(QStringLiteral("valueStack"));

    auto *empty = new QLabel(tr("Select a key to view its value."), this);
    empty->setAlignment(Qt::AlignCenter);
    m_stack->addWidget(empty);

    m_stack->addWidget(buildStringPage());
    m_stack->addWidget(buildHashPage());
    m_stack->addWidget(buildListPage());
    m_stack->addWidget(buildSetPage());
    m_stack->addWidget(buildZSetPage());
    m_stack->addWidget(buildStreamPage());
    m_stack->addWidget(buildJsonPage());
    m_stack->addWidget(buildRawPage());
    layout->addWidget(m_stack, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_statusLabel);

    connect(refreshButton, &QPushButton::clicked, this, [this] {
        if (!m_key.isEmpty())
            loadKey(KeyInfo{m_key, m_type, m_ttlMs});
    });
    connect(ttlButton, &QPushButton::clicked, this, &ValuePanel::editTtl);
    connect(deleteButton, &QPushButton::clicked, this, &ValuePanel::deleteKey);
}

QWidget *ValuePanel::buildStringPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    m_stringEdit = new QPlainTextEdit(page);
    m_stringEdit->setObjectName(QStringLiteral("stringEdit"));
    m_stringEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(m_stringEdit, 1);

    auto *row = new QHBoxLayout;
    auto *saveButton = new QPushButton(tr("Save string"), page);
    saveButton->setObjectName(QStringLiteral("saveStringButton"));
    auto *rangeButton = new QPushButton(tr("Replace range..."), page);
    rangeButton->setObjectName(QStringLiteral("replaceRangeButton"));
    row->addWidget(saveButton);
    row->addWidget(rangeButton);
    m_writeWidgets << saveButton << rangeButton;
    row->addStretch();
    layout->addLayout(row);
    connect(saveButton, &QPushButton::clicked, this, &ValuePanel::saveString);
    connect(rangeButton, &QPushButton::clicked, this, &ValuePanel::partialStringEdit);
    return page;
}

QWidget *ValuePanel::buildHashPage()
{
    m_hashTable = makeTable({tr("Field"), tr("Value")}, this);
    m_hashTable->setObjectName(QStringLiteral("hashTable"));
    m_valueTables << m_hashTable;
    auto *addButton = new QPushButton(tr("Add field"), this);
    auto *editButton = new QPushButton(tr("Edit selected..."), this);
    editButton->setObjectName(QStringLiteral("hashBatchEditButton"));
    auto *deleteButton = new QPushButton(tr("Delete fields"), this);
    auto *reloadButton = new QPushButton(tr("Reload"), this);
    m_hashMoreButton = new QPushButton(tr("Load more"), this);
    m_hashMoreButton->setObjectName(QStringLiteral("hashMoreButton"));
    m_hashMoreButton->setEnabled(false);
    m_writeWidgets << addButton << editButton << deleteButton;
    connect(editButton, &QPushButton::clicked, this, &ValuePanel::editHashSelected);
    connect(m_hashMoreButton, &QPushButton::clicked, this,
            [this] { loadHashPage(m_hashCursor, true); });
    connect(addButton, &QPushButton::clicked, this, &ValuePanel::addHashField);
    connect(deleteButton, &QPushButton::clicked, this, &ValuePanel::deleteHashFields);
    connect(reloadButton, &QPushButton::clicked, this, &ValuePanel::loadHash);
    connect(m_hashTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (m_readOnly || m_loading || !item || item->column() != 1)
            return;
        QTableWidgetItem *fieldItem = m_hashTable->item(item->row(), 0);
        if (!fieldItem)
            return;
        runCommand({QByteArrayLiteral("HSET"), m_key, fieldItem->text().toUtf8(),
                    item->text().toUtf8()},
                   [this](const RespValue &reply, const QString &error) {
                       if (!error.isEmpty() || reply.isError())
                           showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
                       else
                           setStatus(tr("Field updated"));
                   });
    });
    return tablePage(m_hashTable, {addButton, editButton, deleteButton, reloadButton, m_hashMoreButton}, this);
}

QWidget *ValuePanel::buildListPage()
{
    m_listTable = makeTable({tr("#"), tr("Value")}, this);
    m_listTable->setObjectName(QStringLiteral("listTable"));
    m_valueTables << m_listTable;
    auto *headButton = new QPushButton(tr("Push head"), this);
    auto *tailButton = new QPushButton(tr("Push tail"), this);
    auto *editButton = new QPushButton(tr("Edit selected..."), this);
    editButton->setObjectName(QStringLiteral("listBatchEditButton"));
    auto *deleteButton = new QPushButton(tr("Delete rows"), this);
    auto *reloadButton = new QPushButton(tr("Reload"), this);
    m_listMoreButton = new QPushButton(tr("Load more"), this);
    m_listMoreButton->setObjectName(QStringLiteral("listMoreButton"));
    m_listMoreButton->setEnabled(false);
    m_writeWidgets << headButton << tailButton << editButton << deleteButton;
    connect(editButton, &QPushButton::clicked, this, &ValuePanel::editListSelected);
    connect(m_listMoreButton, &QPushButton::clicked, this,
            [this] { loadListPage(m_listOffset, true); });
    connect(headButton, &QPushButton::clicked, this, [this] { addListValue(true); });
    connect(tailButton, &QPushButton::clicked, this, [this] { addListValue(false); });
    connect(deleteButton, &QPushButton::clicked, this, &ValuePanel::deleteListValues);
    connect(reloadButton, &QPushButton::clicked, this, &ValuePanel::loadList);
    connect(m_listTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (m_readOnly || m_loading || !item || item->column() != 1)
            return;
        QTableWidgetItem *indexItem = m_listTable->item(item->row(), 0);
        if (!indexItem)
            return;
        runCommand({QByteArrayLiteral("LSET"), m_key, indexItem->text().toUtf8(),
                    item->text().toUtf8()},
                   [this](const RespValue &reply, const QString &error) {
                       if (!error.isEmpty() || reply.isError())
                           showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
                       else
                           setStatus(tr("List element updated"));
                   });
    });
    return tablePage(m_listTable, {headButton, tailButton, editButton, deleteButton, reloadButton, m_listMoreButton}, this);
}

QWidget *ValuePanel::buildSetPage()
{
    m_setTable = makeTable({tr("Member")}, this);
    m_setTable->setObjectName(QStringLiteral("setTable"));
    m_valueTables << m_setTable;
    auto *addButton = new QPushButton(tr("Add member"), this);
    auto *editButton = new QPushButton(tr("Edit selected..."), this);
    editButton->setObjectName(QStringLiteral("setBatchEditButton"));
    auto *deleteButton = new QPushButton(tr("Delete members"), this);
    auto *reloadButton = new QPushButton(tr("Reload"), this);
    m_setMoreButton = new QPushButton(tr("Load more"), this);
    m_setMoreButton->setObjectName(QStringLiteral("setMoreButton"));
    m_setMoreButton->setEnabled(false);
    m_writeWidgets << addButton << editButton << deleteButton;
    connect(editButton, &QPushButton::clicked, this, &ValuePanel::editSetSelected);
    connect(m_setMoreButton, &QPushButton::clicked, this,
            [this] { loadSetPage(m_setCursor, true); });
    connect(addButton, &QPushButton::clicked, this, &ValuePanel::addSetMember);
    connect(deleteButton, &QPushButton::clicked, this, &ValuePanel::deleteSetMembers);
    connect(reloadButton, &QPushButton::clicked, this, &ValuePanel::loadSet);
    return tablePage(m_setTable, {addButton, editButton, deleteButton, reloadButton, m_setMoreButton}, this);
}

QWidget *ValuePanel::buildZSetPage()
{
    m_zsetTable = makeTable({tr("Member"), tr("Score")}, this);
    m_zsetTable->setObjectName(QStringLiteral("zsetTable"));
    m_valueTables << m_zsetTable;
    auto *addButton = new QPushButton(tr("Add member"), this);
    auto *editButton = new QPushButton(tr("Edit selected..."), this);
    editButton->setObjectName(QStringLiteral("zsetBatchEditButton"));
    auto *deleteButton = new QPushButton(tr("Delete members"), this);
    auto *reloadButton = new QPushButton(tr("Reload"), this);
    m_zsetMoreButton = new QPushButton(tr("Load more"), this);
    m_zsetMoreButton->setObjectName(QStringLiteral("zsetMoreButton"));
    m_zsetMoreButton->setEnabled(false);
    m_writeWidgets << addButton << editButton << deleteButton;
    connect(editButton, &QPushButton::clicked, this, &ValuePanel::editZSetSelected);
    connect(m_zsetMoreButton, &QPushButton::clicked, this,
            [this] { loadZSetPage(m_zsetCursor, true); });
    connect(addButton, &QPushButton::clicked, this, &ValuePanel::addZSetMember);
    connect(deleteButton, &QPushButton::clicked, this, &ValuePanel::deleteZSetMembers);
    connect(reloadButton, &QPushButton::clicked, this, &ValuePanel::loadZSet);
    connect(m_zsetTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (m_readOnly || m_loading || !item || item->column() != 1)
            return;
        QTableWidgetItem *memberItem = m_zsetTable->item(item->row(), 0);
        if (!memberItem)
            return;
        bool ok = false;
        item->text().toDouble(&ok);
        if (!ok) {
            showError(tr("Score must be a number"));
            return;
        }
        runCommand({QByteArrayLiteral("ZADD"), m_key, item->text().toUtf8(),
                    memberItem->text().toUtf8()},
                   [this](const RespValue &reply, const QString &error) {
                       if (!error.isEmpty() || reply.isError())
                           showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
                       else
                           setStatus(tr("Score updated"));
                   });
    });
    return tablePage(m_zsetTable, {addButton, editButton, deleteButton, reloadButton, m_zsetMoreButton}, this);
}

QWidget *ValuePanel::buildStreamPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *tabs = new QTabWidget(page);
    tabs->setObjectName(QStringLiteral("streamTabs"));

    auto *entriesPage = new QWidget(tabs);
    auto *entriesLayout = new QVBoxLayout(entriesPage);
    entriesLayout->setContentsMargins(0, 0, 0, 0);
    m_streamTable = makeTable({tr("ID"), tr("Fields")}, entriesPage);
    m_streamTable->setObjectName(QStringLiteral("streamTable"));
    m_valueTables << m_streamTable;
    entriesLayout->addWidget(m_streamTable, 1);
    auto *entriesBar = new QHBoxLayout;
    auto *addButton = new QPushButton(tr("Add entry"), entriesPage);
    auto *deleteButton = new QPushButton(tr("Delete entries"), entriesPage);
    auto *reloadButton = new QPushButton(tr("Reload"), entriesPage);
    entriesBar->addWidget(addButton);
    entriesBar->addWidget(deleteButton);
    entriesBar->addWidget(reloadButton);
    entriesBar->addStretch();
    entriesLayout->addLayout(entriesBar);
    tabs->addTab(entriesPage, tr("Entries"));

    auto *groupsPage = new QWidget(tabs);
    auto *groupsLayout = new QVBoxLayout(groupsPage);
    groupsLayout->setContentsMargins(0, 0, 0, 0);
    m_streamGroupsTable = makeTable({tr("Group"), tr("Consumers"), tr("Pending"), tr("Last ID")},
                                    groupsPage);
    m_streamGroupsTable->setObjectName(QStringLiteral("streamGroupsTable"));
    m_valueTables << m_streamGroupsTable;
    groupsLayout->addWidget(m_streamGroupsTable, 1);
    auto *groupsBar = new QHBoxLayout;
    auto *createGroupButton = new QPushButton(tr("Create group"), groupsPage);
    auto *deleteGroupButton = new QPushButton(tr("Delete group"), groupsPage);
    auto *pendingButton = new QPushButton(tr("Pending..."), groupsPage);
    auto *claimButton = new QPushButton(tr("Claim..."), groupsPage);
    auto *ackButton = new QPushButton(tr("Ack"), groupsPage);
    groupsBar->addWidget(createGroupButton);
    groupsBar->addWidget(deleteGroupButton);
    groupsBar->addWidget(pendingButton);
    groupsBar->addWidget(claimButton);
    groupsBar->addWidget(ackButton);
    groupsBar->addStretch();
    groupsLayout->addLayout(groupsBar);
    auto *pendingLabel = new QLabel(tr("Pending entries"), groupsPage);
    groupsLayout->addWidget(pendingLabel);
    m_streamPendingTable = makeTable({tr("ID"), tr("Consumer"), tr("Idle"), tr("Deliveries")},
                                     groupsPage);
    m_streamPendingTable->setObjectName(QStringLiteral("streamPendingTable"));
    m_valueTables << m_streamPendingTable;
    groupsLayout->addWidget(m_streamPendingTable, 1);
    tabs->addTab(groupsPage, tr("Groups"));

    m_writeWidgets << addButton << deleteButton << createGroupButton << deleteGroupButton
                   << claimButton << ackButton;
    connect(addButton, &QPushButton::clicked, this, &ValuePanel::addStreamEntry);
    connect(deleteButton, &QPushButton::clicked, this, &ValuePanel::deleteStreamEntries);
    connect(reloadButton, &QPushButton::clicked, this, &ValuePanel::loadStream);
    connect(createGroupButton, &QPushButton::clicked, this, &ValuePanel::createStreamGroup);
    connect(deleteGroupButton, &QPushButton::clicked, this, &ValuePanel::deleteStreamGroup);
    connect(pendingButton, &QPushButton::clicked, this, &ValuePanel::loadPending);
    connect(claimButton, &QPushButton::clicked, this, &ValuePanel::claimStreamEntries);
    connect(ackButton, &QPushButton::clicked, this, &ValuePanel::ackStreamEntries);
    connect(m_streamGroupsTable, &QTableWidget::itemSelectionChanged, this,
            &ValuePanel::loadPending);

    layout->addWidget(tabs);
    return page;
}

QWidget *ValuePanel::buildJsonPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *pathRow = new QHBoxLayout;
    pathRow->addWidget(new QLabel(tr("JSON path"), page));
    m_jsonPathEdit = new QLineEdit(QStringLiteral("$"), page);
    m_jsonPathEdit->setObjectName(QStringLiteral("jsonPathEdit"));
    m_jsonPathEdit->setPlaceholderText(tr("RedisJSON path, e.g. $ or $.user"));
    pathRow->addWidget(m_jsonPathEdit, 1);
    auto *loadPathButton = new QPushButton(tr("Load path"), page);
    pathRow->addWidget(loadPathButton);
    layout->addLayout(pathRow);
    m_jsonEdit = new QPlainTextEdit(page);
    m_jsonEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(m_jsonEdit, 1);
    auto *row = new QHBoxLayout;
    auto *saveButton = new QPushButton(tr("Save JSON"), page);
    saveButton->setObjectName(QStringLiteral("saveJsonButton"));
    row->addWidget(saveButton);
    row->addStretch();
    layout->addLayout(row);
    connect(loadPathButton, &QPushButton::clicked, this, &ValuePanel::loadJson);
    m_writeWidgets << saveButton;
    connect(saveButton, &QPushButton::clicked, this, &ValuePanel::saveJson);
    return page;
}

QWidget *ValuePanel::buildRawPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    m_rawView = new QPlainTextEdit(page);
    m_rawView->setObjectName(QStringLiteral("rawView"));
    m_rawView->setReadOnly(true);
    m_rawView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(m_rawView, 1);
    auto *row = new QHBoxLayout;
    auto *reloadButton = new QPushButton(tr("Reload"), page);
    row->addWidget(reloadButton);
    row->addStretch();
    layout->addLayout(row);
    connect(reloadButton, &QPushButton::clicked, this, [this] { loadRaw(m_type); });
    return page;
}

void ValuePanel::loadKey(const KeyInfo &info)
{
    if (info.key.isEmpty()) {
        clearKey();
        return;
    }
    ++m_generation;
    m_key = info.key;
    m_type = info.type;
    m_ttlMs = info.ttlMs;
    m_hashCursor = QByteArrayLiteral("0");
    m_setCursor = QByteArrayLiteral("0");
    m_zsetCursor = QByteArrayLiteral("0");
    m_listOffset = 0;
    if (m_hashMoreButton) m_hashMoreButton->setEnabled(false);
    if (m_listMoreButton) m_listMoreButton->setEnabled(false);
    if (m_setMoreButton) m_setMoreButton->setEnabled(false);
    if (m_zsetMoreButton) m_zsetMoreButton->setEnabled(false);
    updateHeader();
    m_statusLabel->clear();

    if (m_type == QLatin1String("string")) {
        m_stack->setCurrentIndex(StringPage);
        loadString();
    } else if (m_type == QLatin1String("hash")) {
        m_stack->setCurrentIndex(HashPage);
        loadHash();
    } else if (m_type == QLatin1String("list")) {
        m_stack->setCurrentIndex(ListPage);
        loadList();
    } else if (m_type == QLatin1String("set")) {
        m_stack->setCurrentIndex(SetPage);
        loadSet();
    } else if (m_type == QLatin1String("zset")) {
        m_stack->setCurrentIndex(ZSetPage);
        loadZSet();
    } else if (m_type == QLatin1String("stream")) {
        m_stack->setCurrentIndex(StreamPage);
        loadStream();
    } else if (m_type == QLatin1String("ReJSON-RL") || m_type == QLatin1String("json")) {
        m_stack->setCurrentIndex(JsonPage);
        loadJson();
    } else {
        m_stack->setCurrentIndex(RawPage);
        loadRaw(m_type);
    }
}

void ValuePanel::clearKey()
{
    ++m_generation;
    m_key.clear();
    m_type.clear();
    m_ttlMs = -2;
    m_keyLabel->setText(tr("No key selected"));
    m_typeLabel->clear();
    m_ttlLabel->clear();
    m_statusLabel->clear();
    m_hashCursor = QByteArrayLiteral("0");
    m_setCursor = QByteArrayLiteral("0");
    m_zsetCursor = QByteArrayLiteral("0");
    m_listOffset = 0;
    if (m_hashMoreButton) m_hashMoreButton->setEnabled(false);
    if (m_listMoreButton) m_listMoreButton->setEnabled(false);
    if (m_setMoreButton) m_setMoreButton->setEnabled(false);
    if (m_zsetMoreButton) m_zsetMoreButton->setEnabled(false);
    m_stack->setCurrentIndex(EmptyPage);
}

void ValuePanel::setReadOnly(bool readOnly)
{
    m_readOnly = readOnly;
    for (QWidget *widget : std::as_const(m_writeWidgets))
        widget->setEnabled(!readOnly);
    for (QTableWidget *table : std::as_const(m_valueTables)) {
        table->setEditTriggers(readOnly
                                   ? QAbstractItemView::NoEditTriggers
                                   : (QAbstractItemView::DoubleClicked
                                      | QAbstractItemView::SelectedClicked
                                      | QAbstractItemView::EditKeyPressed));
    }
    if (m_stringEdit)
        m_stringEdit->setReadOnly(readOnly);
    if (m_jsonEdit)
        m_jsonEdit->setReadOnly(readOnly);
}

bool ValuePanel::isReadOnly() const
{
    return m_readOnly;
}

void ValuePanel::updateHeader()
{
    m_keyLabel->setText(displayKey(m_key));
    m_keyLabel->setToolTip(displayKey(m_key));
    m_typeLabel->setText(m_type.isEmpty() ? tr("type: -") : tr("type: %1").arg(m_type));
    const QString ttlText = m_ttlMs == -1 ? QStringLiteral("∞") : formatTtl(m_ttlMs);
    m_ttlLabel->setText(tr("TTL: %1").arg(ttlText));
    m_ttlLabel->setToolTip(formatTtl(m_ttlMs));
}

void ValuePanel::loadString()
{
    const quint64 generation = m_generation;
    QPointer<ValuePanel> guard(this);
    runCommand({QByteArrayLiteral("GET"), m_key},
               [guard, generation, this](const RespValue &reply, const QString &error) {
                   if (!guard || generation != m_generation)
                       return;
                   if (!error.isEmpty()) {
                       showError(error);
                       return;
                   }
                   if (reply.isError()) {
                       showError(QString::fromUtf8(reply.scalar));
                       return;
                   }
                   m_stringEdit->setPlainText(scalarText(reply));
                   setStatus(tr("String loaded"));
               });
}

void ValuePanel::loadHash()
{
    loadHashPage(QByteArrayLiteral("0"), false);
}

void ValuePanel::loadHashPage(const QByteArray &cursor, bool append)
{
    if (m_hashMoreButton)
        m_hashMoreButton->setEnabled(false);
    const quint64 generation = m_generation;
    QPointer<ValuePanel> guard(this);
    runCommand({QByteArrayLiteral("HSCAN"), m_key, cursor, QByteArrayLiteral("COUNT"),
                QByteArrayLiteral("500")},
               [guard, generation, append, this](const RespValue &reply, const QString &error) {
                   if (!guard || generation != m_generation)
                       return;
                   if (!error.isEmpty() || reply.isError()) {
                       showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
                       return;
                   }
                   if (reply.type != RespType::Array || reply.elements.size() != 2
                       || reply.elements[1].type != RespType::Array) {
                       showError(tr("Unexpected HSCAN reply"));
                       return;
                   }
                   const auto &flat = reply.elements[1].elements;
                   m_loading = true;
                   if (!append)
                       m_hashTable->setRowCount(0);
                   for (size_t index = 0; index + 1 < flat.size(); index += 2) {
                       const int row = m_hashTable->rowCount();
                       m_hashTable->insertRow(row);
                       m_hashTable->setItem(row, 0,
                                            readOnlyItem(QString::fromUtf8(flat[index].scalar)));
                       m_hashTable->setItem(row, 1,
                                            new QTableWidgetItem(QString::fromUtf8(flat[index + 1].scalar)));
                   }
                   m_loading = false;
                   m_hashCursor = reply.elements[0].scalar;
                   m_hashMoreButton->setEnabled(m_hashCursor != QByteArrayLiteral("0"));
                   setStatus(tr("%1 fields loaded%2")
                                 .arg(m_hashTable->rowCount())
                                 .arg(m_hashCursor == QByteArrayLiteral("0")
                                          ? QString()
                                          : tr("; more available")));
               });
}

void ValuePanel::loadList()
{
    loadListPage(0, false);
}

void ValuePanel::loadListPage(int offset, bool append)
{
    if (m_listMoreButton)
        m_listMoreButton->setEnabled(false);
    const quint64 generation = m_generation;
    QPointer<ValuePanel> guard(this);
    const int end = offset + 499;
    runCommand({QByteArrayLiteral("LRANGE"), m_key, QByteArray::number(offset),
                QByteArray::number(end)},
               [guard, generation, offset, append, this](const RespValue &reply,
                                                         const QString &error) {
                   if (!guard || generation != m_generation)
                       return;
                   if (!error.isEmpty() || reply.isError()) {
                       showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
                       return;
                   }
                   if (reply.type != RespType::Array) {
                       showError(tr("Unexpected LRANGE reply"));
                       return;
                   }
                   m_loading = true;
                   if (!append)
                       m_listTable->setRowCount(0);
                   for (size_t index = 0; index < reply.elements.size(); ++index) {
                       const int row = m_listTable->rowCount();
                       m_listTable->insertRow(row);
                       m_listTable->setItem(row, 0,
                                            readOnlyItem(QString::number(offset + int(index))));
                       m_listTable->setItem(row, 1,
                                            new QTableWidgetItem(scalarText(reply.elements[index])));
                   }
                   m_listOffset = offset + int(reply.elements.size());
                   m_listMoreButton->setEnabled(reply.elements.size() == 500);
                   m_loading = false;
                   setStatus(tr("%1 elements loaded%2")
                                 .arg(m_listTable->rowCount())
                                 .arg(reply.elements.size() == 500 ? tr("; more available")
                                                                   : QString()));
               });
}

void ValuePanel::loadSet()
{
    loadSetPage(QByteArrayLiteral("0"), false);
}

void ValuePanel::loadSetPage(const QByteArray &cursor, bool append)
{
    if (m_setMoreButton)
        m_setMoreButton->setEnabled(false);
    const quint64 generation = m_generation;
    QPointer<ValuePanel> guard(this);
    runCommand({QByteArrayLiteral("SSCAN"), m_key, cursor, QByteArrayLiteral("COUNT"),
                QByteArrayLiteral("500")},
               [guard, generation, append, this](const RespValue &reply, const QString &error) {
                   if (!guard || generation != m_generation)
                       return;
                   if (!error.isEmpty() || reply.isError()) {
                       showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
                       return;
                   }
                   if (reply.type != RespType::Array || reply.elements.size() != 2
                       || reply.elements[1].type != RespType::Array) {
                       showError(tr("Unexpected SSCAN reply"));
                       return;
                   }
                   m_loading = true;
                   if (!append)
                       m_setTable->setRowCount(0);
                   for (const RespValue &member : reply.elements[1].elements) {
                       const int row = m_setTable->rowCount();
                       m_setTable->insertRow(row);
                       m_setTable->setItem(row, 0, readOnlyItem(scalarText(member)));
                   }
                   m_loading = false;
                   m_setCursor = reply.elements[0].scalar;
                   m_setMoreButton->setEnabled(m_setCursor != QByteArrayLiteral("0"));
                   setStatus(tr("%1 members loaded%2")
                                 .arg(m_setTable->rowCount())
                                 .arg(m_setCursor == QByteArrayLiteral("0")
                                          ? QString()
                                          : tr("; more available")));
               });
}

void ValuePanel::loadZSet()
{
    loadZSetPage(QByteArrayLiteral("0"), false);
}

void ValuePanel::loadZSetPage(const QByteArray &cursor, bool append)
{
    if (m_zsetMoreButton)
        m_zsetMoreButton->setEnabled(false);
    const quint64 generation = m_generation;
    QPointer<ValuePanel> guard(this);
    runCommand({QByteArrayLiteral("ZSCAN"), m_key, cursor, QByteArrayLiteral("COUNT"),
                QByteArrayLiteral("500")},
               [guard, generation, append, this](const RespValue &reply, const QString &error) {
                   if (!guard || generation != m_generation)
                       return;
                   if (!error.isEmpty() || reply.isError()) {
                       showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
                       return;
                   }
                   if (reply.type != RespType::Array || reply.elements.size() != 2
                       || reply.elements[1].type != RespType::Array) {
                       showError(tr("Unexpected ZSCAN reply"));
                       return;
                   }
                   const auto &flat = reply.elements[1].elements;
                   m_loading = true;
                   if (!append)
                       m_zsetTable->setRowCount(0);
                   for (size_t index = 0; index + 1 < flat.size(); index += 2) {
                       const int row = m_zsetTable->rowCount();
                       m_zsetTable->insertRow(row);
                       m_zsetTable->setItem(row, 0, readOnlyItem(scalarText(flat[index])));
                       m_zsetTable->setItem(row, 1, new QTableWidgetItem(scalarText(flat[index + 1])));
                   }
                   m_loading = false;
                   m_zsetCursor = reply.elements[0].scalar;
                   m_zsetMoreButton->setEnabled(m_zsetCursor != QByteArrayLiteral("0"));
                   setStatus(tr("%1 members loaded%2")
                                 .arg(m_zsetTable->rowCount())
                                 .arg(m_zsetCursor == QByteArrayLiteral("0")
                                          ? QString()
                                          : tr("; more available")));
               });
}

void ValuePanel::loadRaw(const QString &type)
{
    m_rawView->setPlainText(tr("Type \"%1\" is not editable in this build. Use the CLI tab for raw commands.")
                                .arg(type));
    if (type != QLatin1String("stream"))
        return;

    const quint64 generation = m_generation;
    QPointer<ValuePanel> guard(this);
    runCommand({QByteArrayLiteral("XRANGE"), m_key, QByteArrayLiteral("-"),
                QByteArrayLiteral("+"), QByteArrayLiteral("COUNT"), QByteArrayLiteral("100")},
               [guard, generation, this](const RespValue &reply, const QString &error) {
                   if (!guard || generation != m_generation)
                       return;
                   if (!error.isEmpty() || reply.isError()) {
                       showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
                       return;
                   }
                   m_rawView->setPlainText(reply.toDisplayString());
                   setStatus(tr("First 100 stream entries loaded"));
               });
}

void ValuePanel::loadStream()
{
    const quint64 generation = m_generation;
    QPointer<ValuePanel> guard(this);
    runCommand({QByteArrayLiteral("XRANGE"), m_key, QByteArrayLiteral("-"),
                QByteArrayLiteral("+"), QByteArrayLiteral("COUNT"), QByteArrayLiteral("200")},
               [guard, generation, this](const RespValue &reply, const QString &error) {
                   if (!guard || generation != m_generation)
                       return;
                   if (!error.isEmpty() || reply.isError()) {
                       showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
                       return;
                   }
                   if (reply.type != RespType::Array) {
                       showError(tr("Unexpected XRANGE reply"));
                       return;
                   }
                   m_loading = true;
                   m_streamTable->setRowCount(0);
                   for (const RespValue &entry : reply.elements) {
                       if (entry.type != RespType::Array || entry.elements.size() < 2)
                           continue;
                       QStringList fields;
                       const RespValue &fieldArray = entry.elements[1];
                       for (size_t index = 0; index + 1 < fieldArray.elements.size(); index += 2) {
                           fields << QString::fromUtf8(fieldArray.elements[index].scalar)
                                         + QStringLiteral("=")
                                         + QString::fromUtf8(fieldArray.elements[index + 1].scalar);
                       }
                       const int row = m_streamTable->rowCount();
                       m_streamTable->insertRow(row);
                       m_streamTable->setItem(row, 0, readOnlyItem(scalarText(entry.elements[0])));
                       m_streamTable->setItem(row, 1,
                                              readOnlyItem(fields.join(QStringLiteral(", "))));
                   }
                   m_loading = false;
                   setStatus(tr("%1 stream entries loaded").arg(m_streamTable->rowCount()));
                   loadStreamGroups();
               });
}

void ValuePanel::loadStreamGroups()
{
    if (m_streamGroupsTable == nullptr)
        return;
    const quint64 generation = m_generation;
    QPointer<ValuePanel> guard(this);
    runCommand({QByteArrayLiteral("XINFO"), QByteArrayLiteral("GROUPS"), m_key},
               [guard, generation, this](const RespValue &reply, const QString &error) {
                   if (!guard || generation != m_generation)
                       return;
                   if (!error.isEmpty() || reply.isError()) {
                       m_streamGroupsTable->setRowCount(0);
                       return;
                   }
                   m_loading = true;
                   m_streamGroupsTable->setRowCount(0);
                   if (reply.type == RespType::Array) {
                       for (const RespValue &entry : reply.elements) {
                           const QHash<QString, QString> fields = responseMap(entry);
                           const int row = m_streamGroupsTable->rowCount();
                           m_streamGroupsTable->insertRow(row);
                           m_streamGroupsTable->setItem(
                               row, 0, readOnlyItem(fields.value(QStringLiteral("name"))));
                           m_streamGroupsTable->setItem(
                               row, 1, readOnlyItem(fields.value(QStringLiteral("consumers"))));
                           m_streamGroupsTable->setItem(
                               row, 2, readOnlyItem(fields.value(QStringLiteral("pending"))));
                           m_streamGroupsTable->setItem(
                               row, 3,
                               readOnlyItem(fields.value(QStringLiteral("last-delivered-id"))));
                       }
                   }
                   m_loading = false;
                   m_streamGroupsTable->resizeColumnsToContents();
               });
}

void ValuePanel::createStreamGroup()
{
    if (m_readOnly)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Create stream group"),
                                               tr("Group name:"), QLineEdit::Normal,
                                               QStringLiteral("workers"), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    QPointer<ValuePanel> guard(this);
    runCommand({QByteArrayLiteral("XGROUP"), QByteArrayLiteral("CREATE"), m_key,
                name.trimmed().toUtf8(), QByteArrayLiteral("$"), QByteArrayLiteral("MKSTREAM")},
               [guard, this](const RespValue &reply, const QString &error) {
                   if (!guard)
                       return;
                   if (!error.isEmpty() || reply.isError())
                       showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
                   else
                       loadStreamGroups();
               });
}

void ValuePanel::deleteStreamGroup()
{
    if (m_readOnly)
        return;
    const auto rows = m_streamGroupsTable->selectionModel()->selectedRows();
    if (rows.isEmpty())
        return;
    const QString group = m_streamGroupsTable->item(rows.first().row(), 0)->text();
    if (QMessageBox::question(this, tr("Delete stream group"),
                              tr("Delete group \"%1\"?").arg(group))
        != QMessageBox::Yes) {
        return;
    }
    QPointer<ValuePanel> guard(this);
    runCommand({QByteArrayLiteral("XGROUP"), QByteArrayLiteral("DESTROY"), m_key,
                group.toUtf8()},
               [guard, this](const RespValue &reply, const QString &error) {
                   if (!guard)
                       return;
                   if (!error.isEmpty() || reply.isError())
                       showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
                   else
                       loadStreamGroups();
               });
}

void ValuePanel::loadPending()
{
    if (m_streamPendingTable == nullptr || m_loading)
        return;
    const auto rows = m_streamGroupsTable->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        m_streamPendingTable->setRowCount(0);
        return;
    }
    const QString group = m_streamGroupsTable->item(rows.first().row(), 0)->text();
    if (group.isEmpty())
        return;
    const quint64 generation = m_generation;
    QPointer<ValuePanel> guard(this);
    runCommand({QByteArrayLiteral("XPENDING"), m_key, group.toUtf8(), QByteArrayLiteral("-"),
                QByteArrayLiteral("+"), QByteArrayLiteral("100")},
               [guard, generation, this](const RespValue &reply, const QString &error) {
                   if (!guard || generation != m_generation)
                       return;
                   if (!error.isEmpty() || reply.isError()) {
                       m_streamPendingTable->setRowCount(0);
                       return;
                   }
                   m_streamPendingTable->setRowCount(0);
                   if (reply.type != RespType::Array)
                       return;
                   for (const RespValue &entry : reply.elements) {
                       if (entry.type != RespType::Array || entry.elements.size() < 4)
                           continue;
                       const int row = m_streamPendingTable->rowCount();
                       m_streamPendingTable->insertRow(row);
                       m_streamPendingTable->setItem(
                           row, 0, readOnlyItem(scalarText(entry.elements[0])));
                       m_streamPendingTable->setItem(
                           row, 1, readOnlyItem(scalarText(entry.elements[1])));
                       m_streamPendingTable->setItem(
                           row, 2, readOnlyItem(scalarText(entry.elements[2])));
                       m_streamPendingTable->setItem(
                           row, 3, readOnlyItem(scalarText(entry.elements[3])));
                   }
                   m_streamPendingTable->resizeColumnsToContents();
               });
}

void ValuePanel::claimStreamEntries()
{
    if (m_readOnly)
        return;
    const auto rows = m_streamPendingTable->selectionModel()->selectedRows();
    const auto groupRows = m_streamGroupsTable->selectionModel()->selectedRows();
    if (rows.isEmpty() || groupRows.isEmpty())
        return;
    const QString group = m_streamGroupsTable->item(groupRows.first().row(), 0)->text();
    bool ok = false;
    const QString consumer = QInputDialog::getText(this, tr("Claim pending entries"),
                                                   tr("Consumer name:"), QLineEdit::Normal,
                                                   QStringLiteral("worker"), &ok);
    if (!ok || consumer.trimmed().isEmpty())
        return;
    QList<QByteArray> args{QByteArrayLiteral("XCLAIM"), m_key, group.toUtf8(),
                           consumer.trimmed().toUtf8(), QByteArrayLiteral("0")};
    for (const QModelIndex &index : rows) {
        if (auto *item = m_streamPendingTable->item(index.row(), 0))
            args.append(item->text().toUtf8());
    }
    if (args.size() <= 5)
        return;
    QPointer<ValuePanel> guard(this);
    runCommand(args, [guard, this](const RespValue &reply, const QString &error) {
        if (!guard)
            return;
        if (!error.isEmpty() || reply.isError())
            showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
        else
            loadPending();
    });
}

void ValuePanel::ackStreamEntries()
{
    if (m_readOnly)
        return;
    const auto rows = m_streamPendingTable->selectionModel()->selectedRows();
    const auto groupRows = m_streamGroupsTable->selectionModel()->selectedRows();
    if (rows.isEmpty() || groupRows.isEmpty())
        return;
    const QString group = m_streamGroupsTable->item(groupRows.first().row(), 0)->text();
    QList<QByteArray> args{QByteArrayLiteral("XACK"), m_key, group.toUtf8()};
    for (const QModelIndex &index : rows) {
        if (auto *item = m_streamPendingTable->item(index.row(), 0))
            args.append(item->text().toUtf8());
    }
    if (args.size() <= 3)
        return;
    QPointer<ValuePanel> guard(this);
    runCommand(args, [guard, this](const RespValue &reply, const QString &error) {
        if (!guard)
            return;
        if (!error.isEmpty() || reply.isError())
            showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
        else
            loadPending();
    });
}

void ValuePanel::addStreamEntry()
{
    if (m_readOnly)
        return;
    bool ok = false;
    const QString fields = QInputDialog::getMultiLineText(
        this, tr("Add stream entry"), tr("Fields, one field=value per line:"), {}, &ok);
    if (!ok)
        return;
    QList<QByteArray> args{QByteArrayLiteral("XADD"), m_key, QByteArrayLiteral("*")};
    for (const QString &line : fields.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0) {
            showError(tr("Invalid field line: %1").arg(line));
            return;
        }
        args.append(line.left(equals).trimmed().toUtf8());
        args.append(line.mid(equals + 1).toUtf8());
    }
    if (args.size() <= 3) {
        showError(tr("At least one field is required"));
        return;
    }
    QPointer<ValuePanel> guard(this);
    runCommand(args, [guard, this](const RespValue &reply, const QString &error) {
        if (!guard)
            return;
        if (!error.isEmpty() || reply.isError())
            showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
        else
            loadStream();
    });
}

void ValuePanel::deleteStreamEntries()
{
    if (m_readOnly)
        return;
    const auto rows = m_streamTable->selectionModel()->selectedRows();
    if (rows.isEmpty())
        return;
    QList<QByteArray> ids;
    for (const QModelIndex &index : rows) {
        if (auto *item = m_streamTable->item(index.row(), 0))
            ids.append(item->text().toUtf8());
    }
    if (ids.isEmpty())
        return;
    if (QMessageBox::question(this, tr("Delete stream entries"),
                              tr("Delete %1 selected entries?").arg(ids.size()))
        != QMessageBox::Yes) {
        return;
    }
    QList<QByteArray> args{QByteArrayLiteral("XDEL"), m_key};
    args += ids;
    QPointer<ValuePanel> guard(this);
    runCommand(args, [guard, this](const RespValue &reply, const QString &error) {
        if (!guard)
            return;
        if (!error.isEmpty() || reply.isError())
            showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
        else
            loadStream();
    });
}

void ValuePanel::loadJson()
{
    const quint64 generation = m_generation;
    const QByteArray path = m_jsonPathEdit->text().trimmed().isEmpty()
        ? QByteArrayLiteral("$")
        : m_jsonPathEdit->text().trimmed().toUtf8();
    QPointer<ValuePanel> guard(this);
    runCommand({QByteArrayLiteral("JSON.GET"), m_key, path},
               [guard, generation, this](const RespValue &reply, const QString &error) {
                   if (!guard || generation != m_generation)
                       return;
                   if (!error.isEmpty() || reply.isError()) {
                       showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
                       return;
                   }
                   m_jsonEdit->setPlainText(QString::fromUtf8(reply.scalar));
                   setStatus(tr("JSON loaded"));
               });
}

void ValuePanel::saveJson()
{
    if (m_readOnly || m_key.isEmpty())
        return;
    if (!QJsonDocument::fromJson(m_jsonEdit->toPlainText().toUtf8()).isObject()
        && !QJsonDocument::fromJson(m_jsonEdit->toPlainText().toUtf8()).isArray()) {
        showError(tr("Invalid JSON document"));
        return;
    }
    const QByteArray path = m_jsonPathEdit->text().trimmed().isEmpty()
        ? QByteArrayLiteral("$")
        : m_jsonPathEdit->text().trimmed().toUtf8();
    QPointer<ValuePanel> guard(this);
    runCommand({QByteArrayLiteral("JSON.SET"), m_key, path,
                m_jsonEdit->toPlainText().toUtf8()},
               [guard, this](const RespValue &reply, const QString &error) {
                   if (!guard)
                       return;
                   if (!error.isEmpty() || reply.isError()) {
                       showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
                       return;
                   }
                   setStatus(tr("JSON saved"));
                   emit keyChanged(m_key);
               });
}

void ValuePanel::saveString()
{
    if (m_readOnly)
        return;
    if (m_key.isEmpty())
        return;
    QPointer<ValuePanel> guard(this);
    runCommand({QByteArrayLiteral("SET"), m_key, m_stringEdit->toPlainText().toUtf8(),
                QByteArrayLiteral("KEEPTTL")},
               [guard, this](const RespValue &reply, const QString &error) {
                   if (!guard)
                       return;
                   if (!error.isEmpty() || reply.isError()) {
                       showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
                       return;
                   }
                   setStatus(tr("String saved"));
                   emit keyChanged(m_key);
               });
}

void ValuePanel::editTtl()
{
    if (m_readOnly)
        return;
    if (m_key.isEmpty())
        return;
    const qint64 current = m_ttlMs > 0 ? m_ttlMs / 1000 : -1;
    bool ok = false;
    const int seconds = QInputDialog::getInt(this, tr("Set TTL"),
                                             tr("TTL in seconds (-1 = persist):"),
                                             static_cast<int>(current), -1, 2147483647, 1, &ok);
    if (!ok)
        return;

    const QList<QByteArray> args = seconds < 0
        ? QList<QByteArray>{QByteArrayLiteral("PERSIST"), m_key}
        : QList<QByteArray>{QByteArrayLiteral("EXPIRE"), m_key, QByteArray::number(seconds)};
    QPointer<ValuePanel> guard(this);
    runCommand(args, [guard, this, seconds](const RespValue &reply, const QString &error) {
        if (!guard)
            return;
        if (!error.isEmpty() || reply.isError()) {
            showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
            return;
        }
        m_ttlMs = seconds < 0 ? -1 : qint64(seconds) * 1000;
        updateHeader();
        emit keyChanged(m_key);
        setStatus(seconds < 0 ? tr("TTL removed") : tr("TTL updated"));
    });
}

void ValuePanel::deleteKey()
{
    if (m_readOnly)
        return;
    if (m_key.isEmpty())
        return;
    if (QMessageBox::question(this, tr("Delete key"),
                              tr("Delete key \"%1\"?").arg(displayKey(m_key)))
        != QMessageBox::Yes) {
        return;
    }

    QPointer<ValuePanel> guard(this);
    const QByteArray key = m_key;
    runCommand({QByteArrayLiteral("UNLINK"), key},
               [guard, this, key](const RespValue &reply, const QString &error) {
                   if (!guard)
                       return;
                   if (!error.isEmpty()) {
                       showError(error);
                       return;
                   }
                   if (reply.isError()) {
                       // Fallback for servers without UNLINK.
                       runCommand({QByteArrayLiteral("DEL"), key},
                                  [guard, this, key](const RespValue &fallback,
                                                     const QString &fallbackError) {
                                      if (!guard)
                                          return;
                                      if (!fallbackError.isEmpty() || fallback.isError()) {
                                          showError(fallbackError.isEmpty()
                                                        ? QString::fromUtf8(fallback.scalar)
                                                        : fallbackError);
                                          return;
                                      }
                                      emit keyDeleted(key);
                                      clearKey();
                                  });
                       return;
                   }
                   emit keyDeleted(key);
                   clearKey();
               });
}

void ValuePanel::partialStringEdit()
{
    if (m_readOnly)
        return;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Replace string range"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *offsetSpin = new QSpinBox(&dialog);
    const QString currentText = m_stringEdit->toPlainText();
    offsetSpin->setRange(0, currentText.toUtf8().size());
    const QTextCursor cursor = m_stringEdit->textCursor();
    offsetSpin->setValue(cursor.hasSelection()
                             ? currentText.left(cursor.selectionStart()).toUtf8().size()
                             : 0);
    form->addRow(tr("Byte offset"), offsetSpin);
    auto *replacement = new QPlainTextEdit(&dialog);
    replacement->setPlainText(cursor.selectedText());
    form->addRow(tr("Replacement"), replacement);
    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QPointer<ValuePanel> guard(this);
    runCommand({QByteArrayLiteral("SETRANGE"), m_key,
                QByteArray::number(offsetSpin->value()),
                replacement->toPlainText().toUtf8()},
               [guard, this](const RespValue &reply, const QString &error) {
        if (!guard)
            return;
        if (!error.isEmpty() || reply.isError())
            showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
        else
            loadString();
    });
}

void ValuePanel::editHashSelected()
{
    if (m_readOnly)
        return;
    const auto rows = selectedPairs(m_hashTable);
    if (rows.isEmpty()) {
        setStatus(tr("Select at least one field"));
        return;
    }
    BatchEditDialog dialog(tr("Edit hash fields"), {tr("Field"), tr("Value")}, rows, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const auto values = dialog.values();
    QList<QByteArray> args{QByteArrayLiteral("HSET"), m_key};
    for (const auto &[field, value] : values) {
        args.append(field.toUtf8());
        args.append(value.toUtf8());
    }
    if (args.size() <= 3)
        return;
    const QList<QByteArray> hsetArgs = args;
    QPointer<ValuePanel> guard(this);
    runCommand(args, [guard, this, hsetArgs](const RespValue &reply, const QString &error) {
        if (!guard)
            return;
        if (!error.isEmpty() || reply.isError()) {
            // Redis < 4 only supports the multi-field HMSET form.
            if (hsetArgs.size() > 4) {
                QList<QByteArray> hmsetArgs = hsetArgs;
                hmsetArgs[0] = QByteArrayLiteral("HMSET");
                runCommand(hmsetArgs, [guard, this](const RespValue &fallback,
                                                     const QString &fallbackError) {
                    if (!guard)
                        return;
                    if (!fallbackError.isEmpty() || fallback.isError())
                        showError(fallbackError.isEmpty()
                                      ? QString::fromUtf8(fallback.scalar)
                                      : fallbackError);
                    else {
                        setStatus(tr("Hash fields updated"));
                        loadHash();
                    }
                });
                return;
            }
            showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
            return;
        }
        setStatus(tr("Hash fields updated"));
        loadHash();
    });
}

void ValuePanel::editListSelected()
{
    if (m_readOnly)
        return;
    const auto rows = selectedPairs(m_listTable);
    if (rows.isEmpty()) {
        setStatus(tr("Select at least one element"));
        return;
    }
    BatchEditDialog dialog(tr("Edit list elements"), {tr("Index"), tr("Value")}, rows, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QList<QList<QByteArray>> commands;
    for (const auto &[index, value] : dialog.values())
        commands.append({QByteArrayLiteral("LSET"), m_key, index.toUtf8(), value.toUtf8()});
    runCommands(commands, [this] {
        setStatus(tr("List elements updated"));
        loadList();
    });
}

void ValuePanel::editSetSelected()
{
    if (m_readOnly)
        return;
    const auto rows = selectedPairs(m_setTable);
    if (rows.isEmpty()) {
        setStatus(tr("Select at least one member"));
        return;
    }
    BatchEditDialog dialog(tr("Edit set members"), {tr("Old member"), tr("New member")}, rows,
                           this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QList<QList<QByteArray>> commands;
    for (const auto &[oldMember, newMember] : dialog.values()) {
        if (oldMember == newMember)
            continue;
        commands.append({QByteArrayLiteral("SREM"), m_key, oldMember.toUtf8()});
        commands.append({QByteArrayLiteral("SADD"), m_key, newMember.toUtf8()});
    }
    if (commands.isEmpty())
        return;
    runCommands(commands, [this] {
        setStatus(tr("Set members updated"));
        loadSet();
    });
}

void ValuePanel::editZSetSelected()
{
    if (m_readOnly)
        return;
    const auto rows = selectedPairs(m_zsetTable);
    if (rows.isEmpty()) {
        setStatus(tr("Select at least one member"));
        return;
    }
    BatchEditDialog dialog(tr("Edit sorted-set scores"), {tr("Member"), tr("Score")}, rows, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QList<QList<QByteArray>> commands;
    for (const auto &[member, score] : dialog.values()) {
        bool ok = false;
        score.toDouble(&ok);
        if (!ok) {
            showError(tr("Score must be a number"));
            return;
        }
        commands.append({QByteArrayLiteral("ZADD"), m_key, score.toUtf8(), member.toUtf8()});
    }
    runCommands(commands, [this] {
        setStatus(tr("Scores updated"));
        loadZSet();
    });
}

void ValuePanel::runCommands(const QList<QList<QByteArray>> &commands,
                             std::function<void()> done)
{
    if (commands.isEmpty()) {
        done();
        return;
    }
    auto remaining = std::make_shared<int>(commands.size());
    auto errorText = std::make_shared<QString>();
    QPointer<ValuePanel> guard(this);
    for (const QList<QByteArray> &command : commands) {
        m_connection->sendCommand(command, [guard, remaining, errorText, done, this](
                                                const RespValue &reply, const QString &error) {
            if (!guard)
                return;
            if (!error.isEmpty() || reply.isError()) {
                if (errorText->isEmpty())
                    *errorText = error.isEmpty() ? QString::fromUtf8(reply.scalar) : error;
            }
            if (--(*remaining) == 0) {
                if (!errorText->isEmpty())
                    showError(*errorText);
                done();
            }
        });
    }
}

void ValuePanel::addHashField()
{
    if (m_readOnly)
        return;
    if (m_key.isEmpty())
        return;
    bool ok = false;
    const QString field = QInputDialog::getText(this, tr("Add hash field"), tr("Field:"),
                                                QLineEdit::Normal, {}, &ok);
    if (!ok || field.isEmpty())
        return;
    const QString value = QInputDialog::getText(this, tr("Add hash field"), tr("Value:"),
                                                QLineEdit::Normal, {}, &ok);
    if (!ok)
        return;

    QPointer<ValuePanel> guard(this);
    runCommand({QByteArrayLiteral("HSET"), m_key, field.toUtf8(), value.toUtf8()},
               [guard, this](const RespValue &reply, const QString &error) {
                   if (!guard)
                       return;
                   if (!error.isEmpty() || reply.isError())
                       showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
                   else
                       loadHash();
               });
}

void ValuePanel::deleteHashFields()
{
    if (m_readOnly)
        return;
    const auto rows = m_hashTable->selectionModel()->selectedRows();
    if (rows.isEmpty())
        return;
    QList<QByteArray> fields;
    for (const QModelIndex &index : rows) {
        if (auto *item = m_hashTable->item(index.row(), 0))
            fields.append(item->text().toUtf8());
    }
    if (fields.isEmpty())
        return;
    if (QMessageBox::question(this, tr("Delete hash fields"),
                              tr("Delete %1 selected field(s)?").arg(fields.size()))
        != QMessageBox::Yes) {
        return;
    }

    QList<QByteArray> args{QByteArrayLiteral("HDEL"), m_key};
    args += fields;
    QPointer<ValuePanel> guard(this);
    runCommand(args, [guard, this](const RespValue &reply, const QString &error) {
        if (!guard)
            return;
        if (!error.isEmpty() || reply.isError())
            showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
        else
            loadHash();
    });
}

void ValuePanel::addListValue(bool head)
{
    if (m_key.isEmpty())
        return;
    bool ok = false;
    const QString value = QInputDialog::getText(this, tr("Add list value"), tr("Value:"),
                                                QLineEdit::Normal, {}, &ok);
    if (!ok)
        return;

    QPointer<ValuePanel> guard(this);
    runCommand({head ? QByteArrayLiteral("LPUSH") : QByteArrayLiteral("RPUSH"), m_key,
                value.toUtf8()},
               [guard, this](const RespValue &reply, const QString &error) {
                   if (!guard)
                       return;
                   if (!error.isEmpty() || reply.isError())
                       showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
                   else
                       loadList();
               });
}

void ValuePanel::deleteListValues()
{
    if (m_readOnly)
        return;
    const auto rows = m_listTable->selectionModel()->selectedRows();
    if (rows.isEmpty())
        return;
    QList<QByteArray> values;
    for (const QModelIndex &index : rows) {
        if (auto *item = m_listTable->item(index.row(), 1))
            values.append(item->text().toUtf8());
    }
    if (values.isEmpty())
        return;
    if (QMessageBox::question(this, tr("Delete list values"),
                              tr("Delete %1 selected value(s)?").arg(values.size()))
        != QMessageBox::Yes) {
        return;
    }

    auto remaining = std::make_shared<int>(values.size());
    QPointer<ValuePanel> guard(this);
    for (const QByteArray &value : values) {
        runCommand({QByteArrayLiteral("LREM"), m_key, QByteArrayLiteral("1"), value},
                   [guard, this, remaining](const RespValue &reply, const QString &error) {
                       if (!guard)
                           return;
                       if (!error.isEmpty() || reply.isError())
                           showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
                       if (--*remaining == 0)
                           loadList();
                   });
    }
}

void ValuePanel::addSetMember()
{
    if (m_readOnly)
        return;
    if (m_key.isEmpty())
        return;
    bool ok = false;
    const QString member = QInputDialog::getText(this, tr("Add set member"), tr("Member:"),
                                                 QLineEdit::Normal, {}, &ok);
    if (!ok)
        return;
    QPointer<ValuePanel> guard(this);
    runCommand({QByteArrayLiteral("SADD"), m_key, member.toUtf8()},
               [guard, this](const RespValue &reply, const QString &error) {
                   if (!guard)
                       return;
                   if (!error.isEmpty() || reply.isError())
                       showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
                   else
                       loadSet();
               });
}

void ValuePanel::deleteSetMembers()
{
    if (m_readOnly)
        return;
    const auto rows = m_setTable->selectionModel()->selectedRows();
    if (rows.isEmpty())
        return;
    QList<QByteArray> members;
    for (const QModelIndex &index : rows) {
        if (auto *item = m_setTable->item(index.row(), 0))
            members.append(item->text().toUtf8());
    }
    if (members.isEmpty())
        return;
    if (QMessageBox::question(this, tr("Delete set members"),
                              tr("Delete %1 selected member(s)?").arg(members.size()))
        != QMessageBox::Yes) {
        return;
    }
    QList<QByteArray> args{QByteArrayLiteral("SREM"), m_key};
    args += members;
    QPointer<ValuePanel> guard(this);
    runCommand(args, [guard, this](const RespValue &reply, const QString &error) {
        if (!guard)
            return;
        if (!error.isEmpty() || reply.isError())
            showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
        else
            loadSet();
    });
}

void ValuePanel::addZSetMember()
{
    if (m_readOnly)
        return;
    if (m_key.isEmpty())
        return;
    bool ok = false;
    const QString member = QInputDialog::getText(this, tr("Add sorted-set member"),
                                                 tr("Member:"), QLineEdit::Normal, {}, &ok);
    if (!ok || member.isEmpty())
        return;
    const QString score = QInputDialog::getText(this, tr("Add sorted-set member"), tr("Score:"),
                                                QLineEdit::Normal, QStringLiteral("0"), &ok);
    if (!ok)
        return;
    bool scoreOk = false;
    score.toDouble(&scoreOk);
    if (!scoreOk) {
        showError(tr("Score must be a number"));
        return;
    }

    QPointer<ValuePanel> guard(this);
    runCommand({QByteArrayLiteral("ZADD"), m_key, score.toUtf8(), member.toUtf8()},
               [guard, this](const RespValue &reply, const QString &error) {
                   if (!guard)
                       return;
                   if (!error.isEmpty() || reply.isError())
                       showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
                   else
                       loadZSet();
               });
}

void ValuePanel::deleteZSetMembers()
{
    if (m_readOnly)
        return;
    const auto rows = m_zsetTable->selectionModel()->selectedRows();
    if (rows.isEmpty())
        return;
    QList<QByteArray> members;
    for (const QModelIndex &index : rows) {
        if (auto *item = m_zsetTable->item(index.row(), 0))
            members.append(item->text().toUtf8());
    }
    if (members.isEmpty())
        return;
    if (QMessageBox::question(this, tr("Delete sorted-set members"),
                              tr("Delete %1 selected member(s)?").arg(members.size()))
        != QMessageBox::Yes) {
        return;
    }
    QList<QByteArray> args{QByteArrayLiteral("ZREM"), m_key};
    args += members;
    QPointer<ValuePanel> guard(this);
    runCommand(args, [guard, this](const RespValue &reply, const QString &error) {
        if (!guard)
            return;
        if (!error.isEmpty() || reply.isError())
            showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
        else
            loadZSet();
    });
}

void ValuePanel::setStatus(const QString &text)
{
    m_statusLabel->setStyleSheet({});
    m_statusLabel->setText(text);
}

void ValuePanel::showError(const QString &error)
{
    m_statusLabel->setStyleSheet(QStringLiteral("color: #da4453;"));
    m_statusLabel->setText(error);
}

void ValuePanel::runCommand(
    const QList<QByteArray> &args,
    std::function<void(const RespValue &, const QString &)> callback)
{
    if (!m_connection) {
        if (callback)
            callback(RespValue{}, tr("Not connected"));
        return;
    }
    QPointer<ValuePanel> guard(this);
    m_connection->sendCommand(args, [guard, callback = std::move(callback)](
                                         const RespValue &reply, const QString &error) {
        if (!guard)
            return;
        if (callback)
            callback(reply, error);
    });
}

} // namespace KeyDis
