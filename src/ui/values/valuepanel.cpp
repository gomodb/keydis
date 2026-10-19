// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "ui/values/valuepanel.h"

#include "core/net/redisclient.h"
#include "core/resp/respvalue.h"
#include "ui/dialogs/batcheditdialog.h"
#include "ui/dialogs/binaryeditdialog.h"
#include "ui/dialogs/bitmapviewdialog.h"
#include "ui/dialogs/compressionviewdialog.h"
#include "ui/dialogs/geosearchdialog.h"
#include "ui/dialogs/hyperloglogviewdialog.h"
#include "ui/dialogs/msgpackviewdialog.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QRegularExpression>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <memory>
#include <utility>

namespace KeyDis {

namespace {

QString hexDump(const QByteArray &data)
{
    if (data.isEmpty())
        return QStringLiteral("(empty)");
    QString output;
    const int bytesPerLine = 16;
    for (int offset = 0; offset < data.size(); offset += bytesPerLine) {
        const int count = qMin(bytesPerLine, data.size() - offset);
        QString hex;
        QString ascii;
        for (int index = 0; index < bytesPerLine; ++index) {
            if (index < count) {
                const auto byte = static_cast<unsigned char>(data.at(offset + index));
                hex += QStringLiteral("%1 ").arg(byte, 2, 16, QLatin1Char('0')).toUpper();
                ascii += (byte >= 32 && byte < 127) ? QChar(byte) : QLatin1Char('.');
            } else {
                hex += QStringLiteral("   ");
            }
        }
        output += QStringLiteral("%1  %2 %3\n")
                      .arg(offset, 8, 16, QLatin1Char('0'))
                      .arg(hex, ascii);
    }
    return output;
}

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

QString jsonScalarText(const QJsonValue &value)
{
    QJsonArray array;
    array.append(value);
    const QByteArray encoded = QJsonDocument(array).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(encoded.mid(1, encoded.size() - 2));
}

QString escapedJsonKey(const QString &key)
{
    QString escaped = key;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return escaped;
}

QString jsonChildPath(const QString &base, const QString &key, bool array, int index)
{
    if (array)
        return base + QLatin1Char('[') + QString::number(index) + QLatin1Char(']');
    static const QRegularExpression simple(QStringLiteral("^[A-Za-z0-9_:-]+$"));
    if (simple.match(key).hasMatch())
        return base + QLatin1Char('.') + key;
    return base + QStringLiteral("[\"") + escapedJsonKey(key) + QStringLiteral("\"]");
}

QString jsonKeyFromPath(const QString &path)
{
    if (path.endsWith(QLatin1Char(']'))) {
        const int open = path.lastIndexOf(QStringLiteral("[\""));
        if (open >= 0) {
            QString key = path.mid(open + 2, path.size() - open - 3);
            key.replace(QStringLiteral("\\\""), QStringLiteral("\""));
            key.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
            return key;
        }
    }
    const int dot = path.lastIndexOf(QLatin1Char('.'));
    return dot >= 0 ? path.mid(dot + 1) : QString();
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
    m_dirtyLabel = new QLabel(this);
    m_dirtyLabel->setObjectName(QStringLiteral("dirtyLabel"));
    m_dirtyLabel->setStyleSheet(QStringLiteral("color: #f67400;"));
    m_dirtyLabel->setVisible(false);
    header->addWidget(m_dirtyLabel);

    auto *refreshButton = new QPushButton(tr("Refresh"), this);
    m_undoButton = new QPushButton(tr("Undo"), this);
    m_undoButton->setObjectName(QStringLiteral("undoButton"));
    m_undoButton->setEnabled(false);
    m_redoButton = new QPushButton(tr("Redo"), this);
    m_redoButton->setObjectName(QStringLiteral("redoButton"));
    m_redoButton->setEnabled(false);
    m_applyButton = new QPushButton(tr("Apply"), this);
    m_applyButton->setObjectName(QStringLiteral("applyButton"));
    m_applyButton->setEnabled(false);
    m_discardButton = new QPushButton(tr("Discard"), this);
    m_discardButton->setObjectName(QStringLiteral("discardPendingButton"));
    m_discardButton->setEnabled(false);
    auto *ttlButton = new QPushButton(tr("TTL..."), this);
    auto *deleteButton = new QPushButton(tr("Delete"), this);
    header->addWidget(refreshButton);
    header->addWidget(m_undoButton);
    header->addWidget(m_redoButton);
    header->addWidget(m_applyButton);
    header->addWidget(m_discardButton);
    header->addWidget(ttlButton);
    header->addWidget(deleteButton);
    m_writeWidgets << m_undoButton << m_redoButton << m_applyButton << m_discardButton
                   << ttlButton << deleteButton;
    connect(m_undoButton, &QPushButton::clicked, this, &ValuePanel::undoLast);
    connect(m_redoButton, &QPushButton::clicked, this, &ValuePanel::redoLast);
    connect(m_applyButton, &QPushButton::clicked, this, &ValuePanel::applyPending);
    connect(m_discardButton, &QPushButton::clicked, this, &ValuePanel::discardPending);
    layout->addLayout(header);

    m_valueFilter = new QLineEdit(this);
    m_valueFilter->setObjectName(QStringLiteral("valueFilter"));
    m_valueFilter->setPlaceholderText(tr("Filter loaded rows"));
    m_valueFilter->setClearButtonEnabled(true);
    m_searchAllButton = new QPushButton(tr("Search all..."), this);
    m_searchAllButton->setObjectName(QStringLiteral("searchAllButton"));
    m_searchAllButton->setToolTip(tr("Load all elements of this key and filter them locally"));
    auto *filterRow = new QHBoxLayout;
    filterRow->addWidget(m_valueFilter, 1);
    filterRow->addWidget(m_searchAllButton);
    layout->addLayout(filterRow);

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
    connect(m_valueFilter, &QLineEdit::textChanged, this, &ValuePanel::filterValueRows);
    connect(m_searchAllButton, &QPushButton::clicked, this, &ValuePanel::searchAllValues);
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
    auto *hexButton = new QPushButton(tr("Hex view..."), page);
    hexButton->setObjectName(QStringLiteral("hexViewButton"));
    auto *bytesButton = new QPushButton(tr("Edit bytes..."), page);
    bytesButton->setObjectName(QStringLiteral("editBytesButton"));
    auto *decodeButton = new QPushButton(tr("Decode..."), page);
    decodeButton->setObjectName(QStringLiteral("decodeButton"));
    auto *msgPackButton = new QPushButton(tr("MsgPack..."), page);
    msgPackButton->setObjectName(QStringLiteral("msgPackButton"));
    m_bitmapButton = new QPushButton(tr("Bitmap..."), page);
    m_bitmapButton->setObjectName(QStringLiteral("bitmapButton"));
    m_hllButton = new QPushButton(tr("HyperLogLog..."), page);
    m_hllButton->setObjectName(QStringLiteral("hllButton"));
    m_hllButton->setVisible(false);
    m_stringDiscardButton = new QPushButton(tr("Discard"), page);
    m_stringDiscardButton->setObjectName(QStringLiteral("discardStringButton"));
    m_stringDiscardButton->setEnabled(false);
    m_stringLoadFullButton = new QPushButton(tr("Load full..."), page);
    m_stringLoadFullButton->setObjectName(QStringLiteral("loadFullStringButton"));
    m_stringLoadFullButton->setVisible(false);
    row->addWidget(saveButton);
    row->addWidget(m_stringDiscardButton);
    row->addWidget(rangeButton);
    row->addWidget(hexButton);
    row->addWidget(bytesButton);
    row->addWidget(decodeButton);
    row->addWidget(msgPackButton);
    row->addWidget(m_bitmapButton);
    row->addWidget(m_hllButton);
    row->addWidget(m_stringLoadFullButton);
    m_writeWidgets << saveButton << m_stringDiscardButton << rangeButton << bytesButton;
    m_stringWriteButtons << saveButton << rangeButton << bytesButton;
    row->addStretch();
    layout->addLayout(row);
    connect(saveButton, &QPushButton::clicked, this, &ValuePanel::saveString);
    connect(rangeButton, &QPushButton::clicked, this, &ValuePanel::partialStringEdit);
    connect(hexButton, &QPushButton::clicked, this, &ValuePanel::showHexView);
    connect(bytesButton, &QPushButton::clicked, this, &ValuePanel::editStringBytes);
    connect(decodeButton, &QPushButton::clicked, this, &ValuePanel::showDecodedValue);
    connect(msgPackButton, &QPushButton::clicked, this, &ValuePanel::showMsgPackValue);
    connect(m_bitmapButton, &QPushButton::clicked, this, &ValuePanel::showBitmapView);
    connect(m_hllButton, &QPushButton::clicked, this, &ValuePanel::showHyperLogLogView);
    connect(m_stringDiscardButton, &QPushButton::clicked, this, &ValuePanel::discardString);
    connect(m_stringLoadFullButton, &QPushButton::clicked, this, &ValuePanel::loadFullString);
    connect(m_stringEdit, &QPlainTextEdit::textChanged, this, &ValuePanel::stringTextChanged);
    return page;
}

QWidget *ValuePanel::buildHashPage()
{
    m_hashTable = makeTable({tr("Field"), tr("Value"), tr("TTL")}, this);
    m_hashTable->setObjectName(QStringLiteral("hashTable"));
    m_valueTables << m_hashTable;
    auto *addButton = new QPushButton(tr("Add field"), this);
    auto *editButton = new QPushButton(tr("Edit selected..."), this);
    editButton->setObjectName(QStringLiteral("hashBatchEditButton"));
    auto *deleteButton = new QPushButton(tr("Delete fields"), this);
    m_hashTtlButton = new QPushButton(tr("Field TTL..."), this);
    m_hashTtlButton->setObjectName(QStringLiteral("hashTtlButton"));
    m_hashTtlButton->setToolTip(tr("Set TTL for selected hash fields"));
    auto *reloadButton = new QPushButton(tr("Reload"), this);
    m_hashMoreButton = new QPushButton(tr("Load more"), this);
    m_hashMoreButton->setObjectName(QStringLiteral("hashMoreButton"));
    m_hashMoreButton->setEnabled(false);
    m_writeWidgets << addButton << editButton << deleteButton << m_hashTtlButton;
    connect(editButton, &QPushButton::clicked, this, &ValuePanel::editHashSelected);
    connect(m_hashTtlButton, &QPushButton::clicked, this, &ValuePanel::editHashFieldTtl);
    connect(m_hashMoreButton, &QPushButton::clicked, this,
            [this] { loadHashPage(m_hashCursor, true); });
    connect(addButton, &QPushButton::clicked, this, &ValuePanel::addHashField);
    connect(deleteButton, &QPushButton::clicked, this, &ValuePanel::deleteHashFields);
    connect(reloadButton, &QPushButton::clicked, this, &ValuePanel::loadHash);
    connect(m_hashTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (m_readOnly || m_loading || !item || item->column() != 1)
            return;
        QTableWidgetItem *fieldItem = m_hashTable->item(item->row(), 0);
        if (!fieldItem || !m_hashOriginal.contains(fieldItem->text()))
            return;
        const QString field = fieldItem->text();
        const QString oldValue = m_hashOriginal.value(field);
        const QString newValue = item->text();
        QList<QList<QByteArray>> commands;
        commands.append({QByteArrayLiteral("HSET"), m_key, field.toUtf8(), newValue.toUtf8()});
        QList<QList<QByteArray>> undo;
        undo.append({QByteArrayLiteral("HSET"), m_key, field.toUtf8(), oldValue.toUtf8()});
        QList<QList<QByteArray>> redo;
        redo.append({QByteArrayLiteral("HSET"), m_key, field.toUtf8(), newValue.toUtf8()});
        stageCommands(tr("hash field"), commands, undo, redo);
        m_hashOriginal.insert(field, newValue);
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
        bool indexOk = false;
        const int index = indexItem->text().toInt(&indexOk);
        if (!indexOk || !m_listOriginal.contains(index))
            return;
        const QString oldValue = m_listOriginal.value(index);
        const QString newValue = item->text();
        QList<QList<QByteArray>> commands;
        commands.append({QByteArrayLiteral("LSET"), m_key, QByteArray::number(index),
                         newValue.toUtf8()});
        QList<QList<QByteArray>> undo;
        undo.append({QByteArrayLiteral("LSET"), m_key, QByteArray::number(index),
                     oldValue.toUtf8()});
        QList<QList<QByteArray>> redo;
        redo.append({QByteArrayLiteral("LSET"), m_key, QByteArray::number(index),
                     newValue.toUtf8()});
        stageCommands(tr("list element"), commands, undo, redo);
        m_listOriginal.insert(index, newValue);
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
    auto *geoButton = new QPushButton(tr("Geo..."), this);
    geoButton->setObjectName(QStringLiteral("geoButton"));
    m_writeWidgets << addButton << editButton << deleteButton;
    connect(editButton, &QPushButton::clicked, this, &ValuePanel::editZSetSelected);
    connect(m_zsetMoreButton, &QPushButton::clicked, this,
            [this] { loadZSetPage(m_zsetCursor, true); });
    connect(addButton, &QPushButton::clicked, this, &ValuePanel::addZSetMember);
    connect(deleteButton, &QPushButton::clicked, this, &ValuePanel::deleteZSetMembers);
    connect(reloadButton, &QPushButton::clicked, this, &ValuePanel::loadZSet);
    connect(geoButton, &QPushButton::clicked, this, [this] {
        if (m_key.isEmpty())
            return;
        GeoSearchDialog dialog(m_connection, m_key, this);
        dialog.exec();
    });
    connect(m_zsetTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (m_readOnly || m_loading || !item || item->column() != 1)
            return;
        QTableWidgetItem *memberItem = m_zsetTable->item(item->row(), 0);
        if (!memberItem || !m_zsetOriginal.contains(memberItem->text()))
            return;
        const QString member = memberItem->text();
        const QString oldScore = m_zsetOriginal.value(member);
        const QString newScore = item->text();
        bool ok = false;
        newScore.toDouble(&ok);
        if (!ok) {
            showError(tr("Score must be a number"));
            m_loading = true;
            item->setText(oldScore);
            m_loading = false;
            return;
        }
        QList<QList<QByteArray>> commands;
        commands.append({QByteArrayLiteral("ZADD"), m_key, newScore.toUtf8(), member.toUtf8()});
        QList<QList<QByteArray>> undo;
        undo.append({QByteArrayLiteral("ZADD"), m_key, oldScore.toUtf8(), member.toUtf8()});
        QList<QList<QByteArray>> redo;
        redo.append({QByteArrayLiteral("ZADD"), m_key, newScore.toUtf8(), member.toUtf8()});
        stageCommands(tr("zset score"), commands, undo, redo);
        m_zsetOriginal.insert(member, newScore);
    });
    return tablePage(m_zsetTable, {addButton, editButton, deleteButton, reloadButton, geoButton,
                                   m_zsetMoreButton}, this);
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

    auto *splitter = new QSplitter(Qt::Vertical, page);
    m_jsonTree = new QTreeWidget(splitter);
    m_jsonTree->setObjectName(QStringLiteral("jsonTree"));
    m_jsonTree->setColumnCount(2);
    m_jsonTree->setHeaderLabels({tr("Path"), tr("Value")});
    m_jsonTree->header()->setStretchLastSection(true);
    m_jsonTree->setAlternatingRowColors(true);
    m_jsonTree->setEditTriggers(QAbstractItemView::DoubleClicked
                                | QAbstractItemView::SelectedClicked
                                | QAbstractItemView::EditKeyPressed);
    splitter->addWidget(m_jsonTree);

    m_jsonEdit = new QPlainTextEdit(splitter);
    m_jsonEdit->setObjectName(QStringLiteral("jsonEdit"));
    m_jsonEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    splitter->addWidget(m_jsonEdit);
    splitter->setSizes({260, 220});
    layout->addWidget(splitter, 1);

    auto *row = new QHBoxLayout;
    auto *refreshTreeButton = new QPushButton(tr("Refresh tree"), page);
    m_jsonAddButton = new QPushButton(tr("Add child"), page);
    m_jsonAddButton->setObjectName(QStringLiteral("jsonAddButton"));
    m_jsonAddButton->setEnabled(false);
    m_jsonDeleteButton = new QPushButton(tr("Delete node"), page);
    m_jsonDeleteButton->setObjectName(QStringLiteral("jsonDeleteButton"));
    m_jsonDeleteButton->setEnabled(false);
    m_jsonRenameButton = new QPushButton(tr("Rename JSON key"), page);
    m_jsonRenameButton->setObjectName(QStringLiteral("jsonRenameButton"));
    m_jsonRenameButton->setEnabled(false);
    auto *formatButton = new QPushButton(tr("Format JSON"), page);
    auto *saveButton = new QPushButton(tr("Save JSON"), page);
    saveButton->setObjectName(QStringLiteral("saveJsonButton"));
    m_jsonDiscardButton = new QPushButton(tr("Discard"), page);
    m_jsonDiscardButton->setObjectName(QStringLiteral("discardJsonButton"));
    m_jsonDiscardButton->setEnabled(false);
    row->addWidget(refreshTreeButton);
    row->addWidget(m_jsonAddButton);
    row->addWidget(m_jsonDeleteButton);
    row->addWidget(m_jsonRenameButton);
    row->addWidget(formatButton);
    row->addWidget(saveButton);
    row->addWidget(m_jsonDiscardButton);
    row->addStretch();
    layout->addLayout(row);
    connect(loadPathButton, &QPushButton::clicked, this, &ValuePanel::loadJson);
    connect(refreshTreeButton, &QPushButton::clicked, this, &ValuePanel::rebuildJsonTree);
    connect(m_jsonAddButton, &QPushButton::clicked, this, &ValuePanel::addJsonChild);
    connect(m_jsonDeleteButton, &QPushButton::clicked, this, &ValuePanel::deleteJsonNode);
    connect(m_jsonRenameButton, &QPushButton::clicked, this, &ValuePanel::renameJsonKey);
    connect(formatButton, &QPushButton::clicked, this, &ValuePanel::formatJson);
    connect(m_jsonTree, &QTreeWidget::itemChanged, this, &ValuePanel::handleJsonTreeEdited);
    connect(m_jsonTree, &QTreeWidget::itemSelectionChanged, this, &ValuePanel::updateJsonButtons);
    m_writeWidgets << m_jsonAddButton << m_jsonDeleteButton << m_jsonRenameButton << saveButton
                   << m_jsonDiscardButton;
    connect(saveButton, &QPushButton::clicked, this, &ValuePanel::saveJson);
    connect(m_jsonDiscardButton, &QPushButton::clicked, this, &ValuePanel::discardJson);
    connect(m_jsonEdit, &QPlainTextEdit::textChanged, this, &ValuePanel::updateDirtyLabel);
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

QByteArray ValuePanel::currentKey() const
{
    return m_key;
}

void ValuePanel::loadKey(const KeyInfo &info)
{
    if (info.key.isEmpty()) {
        clearKey();
        return;
    }
    ++m_generation;
    clearUndo();
    clearPending();
    m_stringDirty = false;
    m_jsonDirty = false;
    if (m_valueFilter) {
        m_valueFilter->clear();
    }
    m_key = info.key;
    m_type = info.type;
    m_stringPartial = false;
    m_stringTotalSize = 0;
    updateStringControls();
    updateDirtyLabel();
    if (m_valueFilter) {
        const bool filterable = m_type == QLatin1String("hash")
            || m_type == QLatin1String("list") || m_type == QLatin1String("set")
            || m_type == QLatin1String("zset") || m_type == QLatin1String("stream");
        m_valueFilter->setVisible(filterable);
        if (m_searchAllButton)
            m_searchAllButton->setVisible(m_type == QLatin1String("hash")
                                          || m_type == QLatin1String("list")
                                          || m_type == QLatin1String("set")
                                          || m_type == QLatin1String("zset"));
    }
    m_ttlMs = info.ttlMs;
    m_hashCursor = QByteArrayLiteral("0");
    m_hashTtlSupported = true;
    if (m_hashTtlButton)
        m_hashTtlButton->setEnabled(!m_readOnly);
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
    clearUndo();
    clearPending();
    if (m_valueFilter) {
        m_valueFilter->clear();
        m_valueFilter->setVisible(false);
    }
    m_stringRaw.clear();
    m_stringLoadedRaw.clear();
    m_stringLoaded.clear();
    m_jsonLoaded.clear();
    m_stringDirty = false;
    m_jsonDirty = false;
    updateDirtyLabel();
    m_key.clear();
    m_type.clear();
    m_ttlMs = -2;
    m_keyLabel->setText(tr("No key selected"));
    m_typeLabel->clear();
    m_ttlLabel->clear();
    m_statusLabel->clear();
    m_hashCursor = QByteArrayLiteral("0");
    m_hashTtlSupported = true;
    m_setCursor = QByteArrayLiteral("0");
    m_zsetCursor = QByteArrayLiteral("0");
    m_listOffset = 0;
    if (m_hashMoreButton) m_hashMoreButton->setEnabled(false);
    if (m_listMoreButton) m_listMoreButton->setEnabled(false);
    if (m_setMoreButton) m_setMoreButton->setEnabled(false);
    if (m_zsetMoreButton) m_zsetMoreButton->setEnabled(false);
    m_stack->setCurrentIndex(EmptyPage);
}

void ValuePanel::searchAllValues()
{
    if (m_key.isEmpty())
        return;
    if (m_type != QLatin1String("hash") && m_type != QLatin1String("list")
        && m_type != QLatin1String("set") && m_type != QLatin1String("zset")) {
        return;
    }
    if (QMessageBox::question(
            this, tr("Search all values"),
            tr("Load all elements of this key into memory? Large collections may block "
               "the connection and use significant memory."))
        != QMessageBox::Yes) {
        return;
    }

    QList<QByteArray> command;
    if (m_type == QLatin1String("hash"))
        command = {QByteArrayLiteral("HGETALL"), m_key};
    else if (m_type == QLatin1String("list"))
        command = {QByteArrayLiteral("LRANGE"), m_key, QByteArrayLiteral("0"), QByteArrayLiteral("-1")};
    else if (m_type == QLatin1String("set"))
        command = {QByteArrayLiteral("SMEMBERS"), m_key};
    else
        command = {QByteArrayLiteral("ZRANGE"), m_key, QByteArrayLiteral("0"), QByteArrayLiteral("-1"),
                   QByteArrayLiteral("WITHSCORES")};

    const quint64 generation = m_generation;
    QPointer<ValuePanel> guard(this);
    setStatus(tr("Loading all values..."));
    runCommand(command, [guard, generation, this](const RespValue &reply, const QString &error) {
        if (!guard || generation != m_generation)
            return;
        if (!error.isEmpty() || reply.isError()) {
            showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
            return;
        }
        m_loading = true;
        if (m_type == QLatin1String("hash")) {
            m_hashTable->setRowCount(0);
            m_hashOriginal.clear();
            QVector<QPair<QByteArray, QByteArray>> pairs;
            if (reply.type == RespType::Map) {
                for (const auto &[field, value] : reply.pairs)
                    pairs.append({field.scalar, value.scalar});
            } else if (reply.type == RespType::Array) {
                for (size_t index = 0; index + 1 < reply.elements.size(); index += 2)
                    pairs.append({reply.elements[index].scalar, reply.elements[index + 1].scalar});
            }
            m_hashCursor = QByteArrayLiteral("0");
            m_hashMoreButton->setEnabled(false);
            for (const auto &[field, value] : pairs) {
                const QString fieldText = QString::fromUtf8(field);
                const QString valueText = QString::fromUtf8(value);
                const int row = m_hashTable->rowCount();
                m_hashTable->insertRow(row);
                m_hashTable->setItem(row, 0, readOnlyItem(fieldText));
                m_hashTable->setItem(row, 1, new QTableWidgetItem(valueText));
                m_hashTable->setItem(row, 2, readOnlyItem(QStringLiteral("-")));
                m_hashOriginal.insert(fieldText, valueText);
            }
        } else if (m_type == QLatin1String("list")) {
            m_listTable->setRowCount(0);
            m_listOriginal.clear();
            for (size_t index = 0; index < reply.elements.size(); ++index) {
                const int itemIndex = int(index);
                const QString value = scalarText(reply.elements[index]);
                const int row = m_listTable->rowCount();
                m_listTable->insertRow(row);
                m_listTable->setItem(row, 0, readOnlyItem(QString::number(itemIndex)));
                m_listTable->setItem(row, 1, new QTableWidgetItem(value));
                m_listOriginal.insert(itemIndex, value);
            }
            m_listOffset = int(reply.elements.size());
            m_listMoreButton->setEnabled(false);
        } else if (m_type == QLatin1String("set")) {
            m_setTable->setRowCount(0);
            for (const RespValue &member : reply.elements) {
                const int row = m_setTable->rowCount();
                m_setTable->insertRow(row);
                m_setTable->setItem(row, 0, readOnlyItem(scalarText(member)));
            }
            m_setMoreButton->setEnabled(false);
        } else {
            m_zsetTable->setRowCount(0);
            m_zsetOriginal.clear();
            QVector<QPair<QString, QString>> pairs;
            if (reply.type == RespType::Array) {
                for (size_t index = 0; index < reply.elements.size(); ++index) {
                    if (reply.elements[index].type == RespType::Array
                        && reply.elements[index].elements.size() == 2) {
                        pairs.append({scalarText(reply.elements[index].elements[0]),
                                      scalarText(reply.elements[index].elements[1])});
                    } else if (index + 1 < reply.elements.size()) {
                        pairs.append({scalarText(reply.elements[index]),
                                      scalarText(reply.elements[index + 1])});
                        ++index;
                    }
                }
            }
            for (const auto &[member, score] : pairs) {
                const int row = m_zsetTable->rowCount();
                m_zsetTable->insertRow(row);
                m_zsetTable->setItem(row, 0, readOnlyItem(member));
                m_zsetTable->setItem(row, 1, new QTableWidgetItem(score));
                m_zsetOriginal.insert(member, score);
            }
            m_zsetMoreButton->setEnabled(false);
        }
        m_loading = false;
        filterValueRows(m_valueFilter ? m_valueFilter->text() : QString());
        if (m_type == QLatin1String("hash"))
            refreshHashFieldTtl();
        setStatus(tr("%1 entries loaded (full key)").arg(
            m_type == QLatin1String("hash") ? m_hashTable->rowCount()
            : m_type == QLatin1String("list") ? m_listTable->rowCount()
            : m_type == QLatin1String("set") ? m_setTable->rowCount()
                                             : m_zsetTable->rowCount()));
    });
}

void ValuePanel::filterValueRows(const QString &text)
{
    const QString needle = text.trimmed();
    const QList<QTableWidget *> tables{m_hashTable, m_listTable, m_setTable, m_zsetTable,
                                       m_streamTable};
    for (QTableWidget *table : tables) {
        if (!table)
            continue;
        for (int row = 0; row < table->rowCount(); ++row) {
            bool match = needle.isEmpty();
            if (!match) {
                for (int column = 0; column < table->columnCount(); ++column) {
                    if (table->item(row, column)
                        && table->item(row, column)->text().contains(needle, Qt::CaseInsensitive)) {
                        match = true;
                        break;
                    }
                }
            }
            table->setRowHidden(row, !match);
        }
    }
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
    if (m_jsonTree) {
        m_jsonTree->setEditTriggers(readOnly ? QAbstractItemView::NoEditTriggers
                                             : QAbstractItemView::DoubleClicked
                                                   | QAbstractItemView::SelectedClicked
                                                   | QAbstractItemView::EditKeyPressed);
    }
    updateStringControls();
    updateJsonButtons();
    updateDirtyLabel();
    updatePendingButtons();
    updateUndoButton();
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
    runCommand({QByteArrayLiteral("STRLEN"), m_key},
               [guard, generation, this](const RespValue &lengthReply, const QString &lengthError) {
                   if (!guard || generation != m_generation)
                       return;
                   constexpr qint64 kPreviewBytes = 1024 * 1024;
                   if (lengthError.isEmpty() && !lengthReply.isError()
                       && lengthReply.type == RespType::Integer
                       && lengthReply.integer > kPreviewBytes) {
                       runCommand({QByteArrayLiteral("GETRANGE"), m_key, QByteArrayLiteral("0"),
                                   QByteArray::number(kPreviewBytes - 1)},
                                  [guard, generation, this, lengthReply](const RespValue &reply,
                                                                         const QString &error) {
                           if (!guard || generation != m_generation)
                               return;
                           if (!error.isEmpty() || reply.isError()) {
                               showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
                               return;
                           }
                           applyStringBytes(reply.scalar, true, lengthReply.integer);
                       });
                       return;
                   }

                   runCommand({QByteArrayLiteral("GET"), m_key},
                              [guard, generation, this](const RespValue &reply,
                                                        const QString &error) {
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
                       applyStringBytes(reply.scalar, false, reply.scalar.size());
                   });
               });
}

void ValuePanel::loadFullString()
{
    if (!m_stringPartial || m_key.isEmpty())
        return;
    if (QMessageBox::question(
            this, tr("Load full string"),
            tr("Load the complete value (%1 bytes) into memory?")
                .arg(QString::number(m_stringTotalSize)))
        != QMessageBox::Yes) {
        return;
    }
    const quint64 generation = m_generation;
    QPointer<ValuePanel> guard(this);
    setStatus(tr("Loading full string..."));
    runCommand({QByteArrayLiteral("GET"), m_key},
               [guard, generation, this](const RespValue &reply, const QString &error) {
        if (!guard || generation != m_generation)
            return;
        if (!error.isEmpty() || reply.isError()) {
            showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
            return;
        }
        applyStringBytes(reply.scalar, false, reply.scalar.size());
    });
}

void ValuePanel::applyStringBytes(const QByteArray &bytes, bool partial, qint64 totalSize)
{
    m_stringRaw = bytes;
    m_stringPartial = partial;
    m_stringTotalSize = totalSize;
    m_stringLoadedRaw = bytes;
    m_stringUpdating = true;
    m_stringEdit->setPlainText(QString::fromUtf8(bytes));
    m_stringUpdating = false;
    m_stringLoaded = m_stringEdit->toPlainText();
    m_stringDirty = false;
    if (m_hllButton)
        m_hllButton->setVisible(m_stringRaw.startsWith("HYLL"));
    updateStringControls();
    updateDirtyLabel();
    if (partial) {
        setStatus(tr("Large value (%1 bytes); showing first %2 KiB. Use Load full.")
                      .arg(QString::number(totalSize))
                      .arg(bytes.size() / 1024));
    } else {
        setStatus(tr("String loaded"));
    }
}

void ValuePanel::updateStringControls()
{
    if (!m_stringEdit)
        return;
    const bool editable = !m_readOnly && !m_stringPartial;
    m_stringEdit->setReadOnly(!editable);
    if (m_stringDiscardButton)
        m_stringDiscardButton->setEnabled(editable && m_stringDirty);
    if (m_stringLoadFullButton) {
        m_stringLoadFullButton->setVisible(m_stringPartial);
        m_stringLoadFullButton->setEnabled(m_stringPartial);
    }
    for (QPushButton *button : m_stringWriteButtons)
        button->setEnabled(editable);
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
                   if (!append) {
                       m_hashTable->setRowCount(0);
                       m_hashOriginal.clear();
                   }
                   for (size_t index = 0; index + 1 < flat.size(); index += 2) {
                       const QString field = QString::fromUtf8(flat[index].scalar);
                       const QString value = QString::fromUtf8(flat[index + 1].scalar);
                       m_hashOriginal.insert(field, value);
                       const int row = m_hashTable->rowCount();
                       m_hashTable->insertRow(row);
                       m_hashTable->setItem(row, 0, readOnlyItem(field));
                       m_hashTable->setItem(row, 1, new QTableWidgetItem(value));
                       m_hashTable->setItem(row, 2, readOnlyItem(QStringLiteral("-")));
                   }
                   if (m_valueFilter)
                       filterValueRows(m_valueFilter->text());
                   m_loading = false;
                   refreshHashFieldTtl();
                   m_hashCursor = reply.elements[0].scalar;
                   m_hashMoreButton->setEnabled(m_hashCursor != QByteArrayLiteral("0"));
                   setStatus(tr("%1 fields loaded%2")
                                 .arg(m_hashTable->rowCount())
                                 .arg(m_hashCursor == QByteArrayLiteral("0")
                                          ? QString()
                                          : tr("; more available")));
               });
}

void ValuePanel::refreshHashFieldTtl()
{
    if (!m_hashTtlSupported || m_hashTable->rowCount() == 0)
        return;
    QList<QByteArray> fields;
    fields.reserve(m_hashTable->rowCount());
    for (int row = 0; row < m_hashTable->rowCount(); ++row) {
        if (QTableWidgetItem *item = m_hashTable->item(row, 0))
            fields.append(item->text().toUtf8());
    }
    if (fields.isEmpty())
        return;
    QList<QByteArray> command{QByteArrayLiteral("HTTL"), m_key, QByteArrayLiteral("FIELDS"),
                              QByteArray::number(fields.size())};
    command.append(fields);
    const quint64 generation = m_generation;
    QPointer<ValuePanel> guard(this);
    runCommand(command, [guard, generation, fields](const RespValue &reply, const QString &error) {
        if (!guard || generation != guard->m_generation)
            return;
        const bool unsupported =
            (!error.isEmpty()
             && error.contains(QStringLiteral("unknown command"), Qt::CaseInsensitive))
            || (reply.isError()
                && QString::fromUtf8(reply.scalar)
                       .contains(QStringLiteral("unknown command"), Qt::CaseInsensitive));
        if (!error.isEmpty() || reply.isError()) {
            if (unsupported) {
                guard->m_hashTtlSupported = false;
                if (guard->m_hashTtlButton)
                    guard->m_hashTtlButton->setEnabled(false);
            }
            for (int row = 0; row < guard->m_hashTable->rowCount(); ++row)
                if (QTableWidgetItem *item = guard->m_hashTable->item(row, 2))
                    item->setText(QStringLiteral("—"));
            return;
        }
        QVector<qint64> values;
        if (reply.type == RespType::Integer && fields.size() == 1) {
            values.append(reply.integer);
        } else if (reply.type == RespType::Array
                   && reply.elements.size() == static_cast<size_t>(fields.size())) {
            for (const RespValue &ttl : reply.elements) {
                if (ttl.type == RespType::Integer)
                    values.append(ttl.integer);
                else
                    values.append(-2);
            }
        } else {
            return;
        }
        for (int index = 0; index < values.size(); ++index) {
            const qint64 ttl = values.at(index);
            const qint64 ms = ttl < 0 ? ttl : ttl * 1000;
            const QString text = ttl == -1 ? tr("persistent")
                : ttl == -2 ? QStringLiteral("—")
                            : formatTtl(ms);
            for (int row = 0; row < guard->m_hashTable->rowCount(); ++row) {
                if (guard->m_hashTable->item(row, 0)->text().toUtf8() == fields.at(index)) {
                    if (QTableWidgetItem *item = guard->m_hashTable->item(row, 2))
                        item->setText(text);
                    break;
                }
            }
        }
    });
}

void ValuePanel::editHashFieldTtl()
{
    if (m_readOnly || m_hashTable->rowCount() == 0)
        return;
    if (!m_pendingCommands.isEmpty()) {
        setStatus(tr("Apply or discard pending changes first"));
        return;
    }
    const auto rows = selectedPairs(m_hashTable);
    if (rows.isEmpty()) {
        setStatus(tr("Select at least one field"));
        return;
    }
    bool ok = false;
    const int seconds = QInputDialog::getInt(
        this, tr("Hash field TTL"), tr("TTL in seconds (-1 = persist):"), -1, -1,
        2147483647, 1, &ok);
    if (!ok)
        return;
    QList<QByteArray> command;
    if (seconds < 0) {
        command = {QByteArrayLiteral("HPERSIST"), m_key, QByteArrayLiteral("FIELDS"),
                   QByteArray::number(rows.size())};
    } else {
        command = {QByteArrayLiteral("HEXPIRE"), m_key, QByteArray::number(seconds),
                   QByteArrayLiteral("FIELDS"), QByteArray::number(rows.size())};
    }
    for (const auto &[field, value] : rows) {
        Q_UNUSED(value)
        command.append(field.toUtf8());
    }
    QPointer<ValuePanel> guard(this);
    runCommand(command, [guard, this](const RespValue &reply, const QString &error) {
        if (!guard)
            return;
        if (!error.isEmpty() || reply.isError()) {
            showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
            return;
        }
        setStatus(tr("Hash field TTL updated"));
        loadHash();
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
                   if (!append) {
                       m_listTable->setRowCount(0);
                       m_listOriginal.clear();
                   }
                   for (size_t index = 0; index < reply.elements.size(); ++index) {
                       const int itemIndex = offset + int(index);
                       const QString value = scalarText(reply.elements[index]);
                       m_listOriginal.insert(itemIndex, value);
                       const int row = m_listTable->rowCount();
                       m_listTable->insertRow(row);
                       m_listTable->setItem(row, 0, readOnlyItem(QString::number(itemIndex)));
                       m_listTable->setItem(row, 1, new QTableWidgetItem(value));
                   }
                   m_listOffset = offset + int(reply.elements.size());
                   m_listMoreButton->setEnabled(reply.elements.size() == 500);
                   if (m_valueFilter)
                       filterValueRows(m_valueFilter->text());
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
                   if (m_valueFilter)
                       filterValueRows(m_valueFilter->text());
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
                   if (!append) {
                       m_zsetTable->setRowCount(0);
                       m_zsetOriginal.clear();
                   }
                   for (size_t index = 0; index + 1 < flat.size(); index += 2) {
                       const QString member = scalarText(flat[index]);
                       const QString score = scalarText(flat[index + 1]);
                       m_zsetOriginal.insert(member, score);
                       const int row = m_zsetTable->rowCount();
                       m_zsetTable->insertRow(row);
                       m_zsetTable->setItem(row, 0, readOnlyItem(member));
                       m_zsetTable->setItem(row, 1, new QTableWidgetItem(score));
                   }
                   if (m_valueFilter)
                       filterValueRows(m_valueFilter->text());
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
                   if (m_valueFilter)
                       filterValueRows(m_valueFilter->text());
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

void ValuePanel::rebuildJsonTree()
{
    if (!m_jsonTree)
        return;
    m_jsonTreeUpdating = true;
    m_jsonTree->clear();
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(m_jsonEdit->toPlainText().toUtf8(),
                                                           &parseError);
    if (parseError.error != QJsonParseError::NoError || document.isNull()) {
        m_jsonTreeUpdating = false;
        return;
    }

    const QString basePath = m_jsonPathEdit->text().trimmed().isEmpty()
        ? QStringLiteral("$")
        : m_jsonPathEdit->text().trimmed();
    const QJsonValue rootValue = document.isObject() ? QJsonValue(document.object())
                                                     : QJsonValue(document.array());
    auto addNode = [&](auto &&self, QTreeWidgetItem *parent, const QJsonValue &value,
                       const QString &path, const QString &label) -> void {
        auto *item = new QTreeWidgetItem(parent);
        item->setText(0, label);
        item->setData(0, Qt::UserRole, path);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        item->setData(0, Qt::UserRole + 1, value.isObject() ? QStringLiteral("object")
                                                             : value.isArray()
                                                                   ? QStringLiteral("array")
                                                                   : QStringLiteral("scalar"));
        if (value.isObject()) {
            item->setText(1, tr("{…}"));
            const QJsonObject object = value.toObject();
            for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
                const QString childPath = jsonChildPath(path, it.key(), false, 0);
                self(self, item, it.value(), childPath, childPath);
            }
        } else if (value.isArray()) {
            item->setText(1, tr("[…]"));
            const QJsonArray array = value.toArray();
            for (int index = 0; index < array.size(); ++index) {
                const QString childPath = jsonChildPath(path, {}, true, index);
                self(self, item, array.at(index), childPath, childPath);
            }
        } else {
            item->setText(1, jsonScalarText(value));
            item->setFlags(item->flags() | Qt::ItemIsEditable);
        }
        item->setExpanded(true);
    };

    auto *root = new QTreeWidgetItem(m_jsonTree);
    root->setText(0, basePath);
    root->setData(0, Qt::UserRole, basePath);
    root->setFlags(root->flags() & ~Qt::ItemIsEditable);
    root->setData(0, Qt::UserRole + 1, rootValue.isObject() ? QStringLiteral("object")
                                                             : rootValue.isArray()
                                                                   ? QStringLiteral("array")
                                                                   : QStringLiteral("scalar"));
    if (rootValue.isObject()) {
        root->setText(1, tr("{…}"));
        const QJsonObject object = rootValue.toObject();
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            const QString path = jsonChildPath(basePath, it.key(), false, 0);
            addNode(addNode, root, it.value(), path, path);
        }
    } else {
        root->setText(1, tr("[…]"));
        const QJsonArray array = rootValue.toArray();
        for (int index = 0; index < array.size(); ++index) {
            const QString path = jsonChildPath(basePath, {}, true, index);
            addNode(addNode, root, array.at(index), path, path);
        }
    }
    root->setExpanded(true);
    m_jsonTree->expandAll();
    m_jsonTree->setCurrentItem(root);
    m_jsonTreeUpdating = false;
    updateJsonButtons();
}

void ValuePanel::handleJsonTreeEdited(QTreeWidgetItem *item, int column)
{
    if (m_jsonTreeUpdating || m_readOnly || !item || column != 1)
        return;
    const QString path = item->data(0, Qt::UserRole).toString();
    if (path.isEmpty())
        return;
    QJsonParseError parseError{};
    const QJsonDocument document =
        QJsonDocument::fromJson("[" + item->text(1).toUtf8() + "]", &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()
        || document.array().size() != 1) {
        showError(tr("Invalid JSON value"));
        rebuildJsonTree();
        return;
    }
    const QByteArray encoded = QJsonDocument(document.array()).toJson(QJsonDocument::Compact);
    const QByteArray value = encoded.mid(1, encoded.size() - 2);
    QPointer<ValuePanel> guard(this);
    runCommand({QByteArrayLiteral("JSON.SET"), m_key, path.toUtf8(), value},
               [guard, this](const RespValue &reply, const QString &error) {
                   if (!guard)
                       return;
                   if (!error.isEmpty() || reply.isError()) {
                       showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
                       rebuildJsonTree();
                       return;
                   }
                   setStatus(tr("JSON path updated"));
                   loadJson();
               });
}

void ValuePanel::updateJsonButtons()
{
    if (!m_jsonAddButton || !m_jsonDeleteButton)
        return;
    QTreeWidgetItem *item = m_jsonTree ? m_jsonTree->currentItem() : nullptr;
    const QString type = item ? item->data(0, Qt::UserRole + 1).toString() : QString();
    const bool container = type == QLatin1String("object") || type == QLatin1String("array");
    const bool root = item && m_jsonTree && item == m_jsonTree->topLevelItem(0);
    QTreeWidgetItem *parent = item ? item->parent() : nullptr;
    const bool objectChild = parent
        && parent->data(0, Qt::UserRole + 1).toString() == QLatin1String("object");
    m_jsonAddButton->setEnabled(!m_readOnly && container);
    m_jsonDeleteButton->setEnabled(!m_readOnly && item && !root);
    m_jsonRenameButton->setEnabled(!m_readOnly && item && !root && objectChild);
}

void ValuePanel::showDecodedValue()
{
    if (m_key.isEmpty())
        return;
    CompressionViewDialog dialog(tr("Decoded value: %1").arg(displayKey(m_key)), m_stringRaw, this);
    dialog.exec();
}

void ValuePanel::showBitmapView()
{
    if (m_key.isEmpty())
        return;
    BitmapViewDialog dialog(tr("Bitmap: %1").arg(displayKey(m_key)), m_stringRaw, this);
    dialog.exec();
}

void ValuePanel::showHyperLogLogView()
{
    if (m_key.isEmpty() || !m_stringRaw.startsWith("HYLL"))
        return;
    HyperLogLogViewDialog dialog(m_connection, m_key, this);
    dialog.exec();
}

void ValuePanel::showMsgPackValue()
{
    if (m_key.isEmpty())
        return;
    MsgPackViewDialog dialog(tr("MessagePack value: %1").arg(displayKey(m_key)), m_stringRaw, this);
    dialog.exec();
}

void ValuePanel::editStringBytes()
{
    if (m_readOnly || m_stringPartial || m_key.isEmpty())
        return;
    BinaryEditDialog dialog(tr("Edit bytes: %1").arg(displayKey(m_key)), m_stringRaw, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_stringRaw = dialog.bytes();
    m_stringUpdating = true;
    m_stringEdit->setPlainText(QString::fromUtf8(m_stringRaw));
    m_stringUpdating = false;
    updateDirtyLabel();
    setStatus(tr("Bytes modified; click Save string"));
}

void ValuePanel::stringTextChanged()
{
    if (m_stringUpdating || m_stringPartial)
        return;
    m_stringRaw = m_stringEdit->toPlainText().toUtf8();
    updateDirtyLabel();
}

void ValuePanel::showHexView()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Hex view: %1").arg(displayKey(m_key)));
    dialog.resize(760, 520);
    auto *layout = new QVBoxLayout(&dialog);
    auto *view = new QPlainTextEdit(&dialog);
    view->setReadOnly(true);
    view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    view->setPlainText(hexDump(m_stringRaw));
    layout->addWidget(view, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

void ValuePanel::addJsonChild()
{
    if (m_readOnly || !m_jsonTree)
        return;
    QTreeWidgetItem *item = m_jsonTree->currentItem();
    if (!item)
        return;
    const QString type = item->data(0, Qt::UserRole + 1).toString();
    const QString basePath = item->data(0, Qt::UserRole).toString();
    if (basePath.isEmpty() || (type != QLatin1String("object") && type != QLatin1String("array"))) {
        setStatus(tr("Select an object or array node"));
        return;
    }

    bool ok = false;
    QString key;
    if (type == QLatin1String("object")) {
        key = QInputDialog::getText(this, tr("Add JSON child"), tr("Key name:"),
                                    QLineEdit::Normal, {}, &ok);
        if (!ok || key.trimmed().isEmpty())
            return;
    }
    const QString valueText = QInputDialog::getMultiLineText(
        this, tr("Add JSON child"), tr("JSON value:"), QStringLiteral("\"value\""), &ok);
    if (!ok)
        return;
    QJsonParseError parseError{};
    const QJsonDocument document =
        QJsonDocument::fromJson("[" + valueText.toUtf8() + "]", &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()
        || document.array().size() != 1) {
        showError(tr("Invalid JSON value"));
        return;
    }
    const QByteArray encoded = QJsonDocument(document.array()).toJson(QJsonDocument::Compact);
    const QByteArray value = encoded.mid(1, encoded.size() - 2);

    QList<QByteArray> command;
    if (type == QLatin1String("object")) {
        command = {QByteArrayLiteral("JSON.SET"), m_key,
                   jsonChildPath(basePath, key.trimmed(), false, 0).toUtf8(), value};
    } else {
        command = {QByteArrayLiteral("JSON.ARRAPPEND"), m_key, basePath.toUtf8(), value};
    }

    QPointer<ValuePanel> guard(this);
    runCommand(command, [guard, this](const RespValue &reply, const QString &error) {
        if (!guard)
            return;
        if (!error.isEmpty() || reply.isError()) {
            showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
            return;
        }
        setStatus(tr("JSON child added"));
        loadJson();
    });
}

void ValuePanel::deleteJsonNode()
{
    if (m_readOnly || !m_jsonTree)
        return;
    QTreeWidgetItem *item = m_jsonTree->currentItem();
    if (!item || item == m_jsonTree->topLevelItem(0))
        return;
    const QString path = item->data(0, Qt::UserRole).toString();
    if (path.isEmpty())
        return;
    if (QMessageBox::question(this, tr("Delete JSON node"),
                              tr("Delete JSON node %1?").arg(path))
        != QMessageBox::Yes) {
        return;
    }
    QPointer<ValuePanel> guard(this);
    runCommand({QByteArrayLiteral("JSON.DEL"), m_key, path.toUtf8()},
               [guard, this](const RespValue &reply, const QString &error) {
        if (!guard)
            return;
        if (!error.isEmpty() || reply.isError()) {
            showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
            return;
        }
        setStatus(tr("JSON node deleted"));
        loadJson();
    });
}

void ValuePanel::renameJsonKey()
{
    if (m_readOnly || !m_jsonTree)
        return;
    QTreeWidgetItem *item = m_jsonTree->currentItem();
    if (!item || !item->parent()
        || item->parent()->data(0, Qt::UserRole + 1).toString() != QLatin1String("object")) {
        return;
    }
    const QString oldPath = item->data(0, Qt::UserRole).toString();
    const QString parentPath = item->parent()->data(0, Qt::UserRole).toString();
    const QString oldKey = jsonKeyFromPath(oldPath);
    if (oldPath.isEmpty() || parentPath.isEmpty() || oldKey.isEmpty())
        return;

    bool ok = false;
    const QString newKey = QInputDialog::getText(this, tr("Rename JSON key"), tr("New key name:"),
                                                 QLineEdit::Normal, oldKey, &ok);
    if (!ok || newKey.trimmed().isEmpty() || newKey.trimmed() == oldKey)
        return;
    const QString newPath = jsonChildPath(parentPath, newKey.trimmed(), false, 0);
    for (int index = 0; index < item->parent()->childCount(); ++index) {
        QTreeWidgetItem *sibling = item->parent()->child(index);
        if (sibling != item && sibling->data(0, Qt::UserRole).toString() == newPath) {
            if (QMessageBox::question(
                    this, tr("Rename JSON key"),
                    tr("A property with this name already exists and will be overwritten. Continue?"))
                != QMessageBox::Yes) {
                return;
            }
            break;
        }
    }

    QPointer<ValuePanel> guard(this);
    runCommand({QByteArrayLiteral("JSON.GET"), m_key, oldPath.toUtf8()},
               [guard, this, newPath, oldPath](const RespValue &reply, const QString &error) {
        if (!guard)
            return;
        if (!error.isEmpty() || reply.isError()) {
            showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
            return;
        }
        const QByteArray value = reply.scalar;
        runCommands({{QByteArrayLiteral("JSON.SET"), m_key, newPath.toUtf8(), value},
                     {QByteArrayLiteral("JSON.DEL"), m_key, oldPath.toUtf8()}},
                    [guard, this](bool success) {
            if (!guard || !success)
                return;
            setStatus(tr("JSON key renamed"));
            loadJson();
        });
    });
}

void ValuePanel::formatJson()
{
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(m_jsonEdit->toPlainText().toUtf8(),
                                                           &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        showError(tr("Invalid JSON document"));
        return;
    }
    m_jsonEdit->setPlainText(QString::fromUtf8(document.toJson(QJsonDocument::Indented)));
    rebuildJsonTree();
    setStatus(tr("JSON formatted"));
}

void ValuePanel::loadJson()
{
    const quint64 generation = m_generation;
    const QByteArray path = m_jsonPathEdit->text().trimmed().isEmpty()
        ? QByteArrayLiteral("$")
        : m_jsonPathEdit->text().trimmed().toUtf8();
    QList<QByteArray> command{QByteArrayLiteral("JSON.GET"), m_key};
    if (path != QByteArrayLiteral("$"))
        command.append(path);
    QPointer<ValuePanel> guard(this);
    runCommand(command,
               [guard, generation, this](const RespValue &reply, const QString &error) {
                   if (!guard || generation != m_generation)
                       return;
                   if (!error.isEmpty() || reply.isError()) {
                       showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
                       return;
                   }
                   m_jsonEdit->setPlainText(QString::fromUtf8(reply.scalar));
                   m_jsonLoaded = m_jsonEdit->toPlainText();
                   m_jsonDirty = false;
                   updateDirtyLabel();
                   rebuildJsonTree();
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
                   m_jsonLoaded = m_jsonEdit->toPlainText();
                   m_jsonDirty = false;
                   updateDirtyLabel();
                   setStatus(tr("JSON saved"));
                   rebuildJsonTree();
                   emit keyChanged(m_key);
               });
}

void ValuePanel::saveString()
{
    if (m_readOnly || m_stringPartial)
        return;
    if (m_key.isEmpty())
        return;
    const QByteArray bytes = m_stringRaw;
    QPointer<ValuePanel> guard(this);
    runCommand({QByteArrayLiteral("SET"), m_key, bytes, QByteArrayLiteral("KEEPTTL")},
               [guard, this, bytes](const RespValue &reply, const QString &error) {
                   if (!guard)
                       return;
                   if (!error.isEmpty() || reply.isError()) {
                       showError(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
                       return;
                   }
                   if (m_stringRaw == bytes) {
                       m_stringLoadedRaw = bytes;
                       m_stringLoaded = m_stringEdit->toPlainText();
                       m_stringDirty = false;
                       updateDirtyLabel();
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
    if (!m_pendingCommands.isEmpty()) {
        setStatus(tr("Apply or discard pending changes first"));
        return;
    }
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
    if (!m_pendingCommands.isEmpty()) {
        setStatus(tr("Apply or discard pending changes first"));
        return;
    }
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
    if (m_readOnly || m_stringPartial)
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
    QList<QByteArray> forward{QByteArrayLiteral("HSET"), m_key};
    QList<QByteArray> inverse{QByteArrayLiteral("HSET"), m_key};
    for (const auto &[field, value] : values) {
        forward.append(field.toUtf8());
        forward.append(value.toUtf8());
        inverse.append(field.toUtf8());
        for (const auto &[oldField, oldValue] : rows) {
            if (oldField == field)
                inverse.append(oldValue.toUtf8());
        }
    }
    if (forward.size() <= 3)
        return;
    stageCommands(tr("hash fields"), {forward}, {inverse}, {forward});
    m_loading = true;
    for (const auto &[field, value] : values) {
        for (int row = 0; row < m_hashTable->rowCount(); ++row) {
            if (m_hashTable->item(row, 0)->text() == field) {
                m_hashTable->item(row, 1)->setText(value);
                break;
            }
        }
        m_hashOriginal.insert(field, value);
    }
    m_loading = false;
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
    QList<QList<QByteArray>> inverse;
    for (const auto &[index, value] : dialog.values()) {
        commands.append({QByteArrayLiteral("LSET"), m_key, index.toUtf8(), value.toUtf8()});
        for (const auto &[oldIndex, oldValue] : rows) {
            if (oldIndex == index)
                inverse.append({QByteArrayLiteral("LSET"), m_key, index.toUtf8(),
                                oldValue.toUtf8()});
        }
    }
    if (commands.isEmpty())
        return;
    stageCommands(tr("list elements"), commands, inverse, commands);
    m_loading = true;
    for (const auto &[index, value] : dialog.values()) {
        const int itemIndex = index.toInt();
        for (int row = 0; row < m_listTable->rowCount(); ++row) {
            if (m_listTable->item(row, 0)->text().toInt() == itemIndex) {
                m_listTable->item(row, 1)->setText(value);
                break;
            }
        }
        m_listOriginal.insert(itemIndex, value);
    }
    m_loading = false;
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
    QList<QList<QByteArray>> inverse;
    for (const auto &[oldMember, newMember] : dialog.values()) {
        if (oldMember == newMember)
            continue;
        commands.append({QByteArrayLiteral("SREM"), m_key, oldMember.toUtf8()});
        commands.append({QByteArrayLiteral("SADD"), m_key, newMember.toUtf8()});
        inverse.prepend({QByteArrayLiteral("SADD"), m_key, oldMember.toUtf8()});
        inverse.prepend({QByteArrayLiteral("SREM"), m_key, newMember.toUtf8()});
    }
    if (commands.isEmpty())
        return;
    stageCommands(tr("set members"), commands, inverse, commands);
    m_loading = true;
    for (const auto &[oldMember, newMember] : dialog.values()) {
        for (int row = 0; row < m_setTable->rowCount(); ++row) {
            if (m_setTable->item(row, 0)->text() == oldMember) {
                m_setTable->item(row, 0)->setText(newMember);
                break;
            }
        }
    }
    m_loading = false;
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
    QList<QList<QByteArray>> inverse;
    for (const auto &[member, score] : dialog.values()) {
        bool ok = false;
        score.toDouble(&ok);
        if (!ok) {
            showError(tr("Score must be a number"));
            return;
        }
        commands.append({QByteArrayLiteral("ZADD"), m_key, score.toUtf8(), member.toUtf8()});
        for (const auto &[oldMember, oldScore] : rows) {
            if (oldMember == member)
                inverse.append({QByteArrayLiteral("ZADD"), m_key, oldScore.toUtf8(),
                                member.toUtf8()});
        }
    }
    if (commands.isEmpty())
        return;
    stageCommands(tr("scores"), commands, inverse, commands);
    m_loading = true;
    for (const auto &[member, score] : dialog.values()) {
        for (int row = 0; row < m_zsetTable->rowCount(); ++row) {
            if (m_zsetTable->item(row, 0)->text() == member) {
                m_zsetTable->item(row, 1)->setText(score);
                break;
            }
        }
        m_zsetOriginal.insert(member, score);
    }
    m_loading = false;
}

void ValuePanel::runCommands(const QList<QList<QByteArray>> &commands,
                             std::function<void(bool)> done)
{
    if (commands.isEmpty()) {
        done(true);
        return;
    }
    QPointer<ValuePanel> guard(this);
    m_connection->sendTransaction(commands, [guard, done, this](const RespValue &reply,
                                                                const QString &error) {
        if (!guard)
            return;
        if (!error.isEmpty() || reply.isError() || reply.isNull()) {
            const QString message = !error.isEmpty()
                ? error
                : (reply.isNull() ? tr("Transaction aborted")
                                  : QString::fromUtf8(reply.scalar));
            showError(message);
            done(false);
            return;
        }
        done(true);
    });
}

void ValuePanel::addHashField()
{
    if (m_readOnly)
        return;
    if (!m_pendingCommands.isEmpty()) {
        setStatus(tr("Apply or discard pending changes first"));
        return;
    }
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
    if (!m_pendingCommands.isEmpty()) {
        setStatus(tr("Apply or discard pending changes first"));
        return;
    }
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
    if (m_readOnly || m_key.isEmpty())
        return;
    if (!m_pendingCommands.isEmpty()) {
        setStatus(tr("Apply or discard pending changes first"));
        return;
    }
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
    if (!m_pendingCommands.isEmpty()) {
        setStatus(tr("Apply or discard pending changes first"));
        return;
    }
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
    if (!m_pendingCommands.isEmpty()) {
        setStatus(tr("Apply or discard pending changes first"));
        return;
    }
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
    if (!m_pendingCommands.isEmpty()) {
        setStatus(tr("Apply or discard pending changes first"));
        return;
    }
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
    if (!m_pendingCommands.isEmpty()) {
        setStatus(tr("Apply or discard pending changes first"));
        return;
    }
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
    if (!m_pendingCommands.isEmpty()) {
        setStatus(tr("Apply or discard pending changes first"));
        return;
    }
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

void ValuePanel::pushUndo(const QString &label, const QList<QList<QByteArray>> &undoCommands,
                          const QList<QList<QByteArray>> &redoCommands)
{
    if (undoCommands.isEmpty())
        return;
    m_undoActions.prepend({label, undoCommands, redoCommands});
    m_redoActions.clear();
    while (m_undoActions.size() > 20)
        m_undoActions.removeLast();
    updateUndoButton();
}

void ValuePanel::undoLast()
{
    if (m_readOnly || m_undoActions.isEmpty())
        return;
    const UndoAction action = m_undoActions.takeFirst();
    QPointer<ValuePanel> guard(this);
    runCommands(action.undoCommands, [guard, this, action](bool success) {
        if (!guard)
            return;
        if (!success) {
            m_undoActions.prepend(action);
            updateUndoButton();
            return;
        }
        if (!action.redoCommands.isEmpty()) {
            m_redoActions.prepend(action);
            while (m_redoActions.size() > 20)
                m_redoActions.removeLast();
        }
        setStatus(tr("Undone: %1").arg(action.label));
        if (m_type == QLatin1String("hash"))
            loadHash();
        else if (m_type == QLatin1String("list"))
            loadList();
        else if (m_type == QLatin1String("set"))
            loadSet();
        else if (m_type == QLatin1String("zset"))
            loadZSet();
        updateUndoButton();
    });
}

void ValuePanel::redoLast()
{
    if (m_readOnly || m_redoActions.isEmpty())
        return;
    const UndoAction action = m_redoActions.takeFirst();
    QPointer<ValuePanel> guard(this);
    runCommands(action.redoCommands, [guard, this, action](bool success) {
        if (!guard)
            return;
        if (!success) {
            m_redoActions.prepend(action);
            updateUndoButton();
            return;
        }
        m_undoActions.prepend(action);
        while (m_undoActions.size() > 20)
            m_undoActions.removeLast();
        setStatus(tr("Redone: %1").arg(action.label));
        if (m_type == QLatin1String("hash"))
            loadHash();
        else if (m_type == QLatin1String("list"))
            loadList();
        else if (m_type == QLatin1String("set"))
            loadSet();
        else if (m_type == QLatin1String("zset"))
            loadZSet();
        updateUndoButton();
    });
}

void ValuePanel::clearUndo()
{
    m_undoActions.clear();
    m_redoActions.clear();
    updateUndoButton();
}

void ValuePanel::updateUndoButton()
{
    const bool pending = !m_pendingCommands.isEmpty();
    if (m_undoButton)
        m_undoButton->setEnabled(!m_readOnly && !pending && !m_undoActions.isEmpty());
    if (m_redoButton)
        m_redoButton->setEnabled(!m_readOnly && !pending && !m_redoActions.isEmpty());
}

void ValuePanel::stageCommands(const QString &label,
                               const QList<QList<QByteArray>> &commands,
                               const QList<QList<QByteArray>> &undoCommands,
                               const QList<QList<QByteArray>> &redoCommands)
{
    if (commands.isEmpty())
        return;
    m_pendingCommands += commands;
    m_pendingUndo = undoCommands + m_pendingUndo;
    m_pendingRedo += redoCommands;
    m_pendingLabel = label;
    updatePendingButtons();
    updateDirtyLabel();
    setStatus(tr("Pending changes; click Apply"));
}

void ValuePanel::applyPending()
{
    if (m_readOnly || m_pendingCommands.isEmpty())
        return;
    const QList<QList<QByteArray>> commands = m_pendingCommands;
    const QList<QList<QByteArray>> undoCommands = m_pendingUndo;
    const QList<QList<QByteArray>> redoCommands = m_pendingRedo;
    const QString label = m_pendingLabel;
    QPointer<ValuePanel> guard(this);
    runCommands(commands, [guard, this, label, undoCommands, redoCommands](bool success) {
        if (!guard || !success)
            return;
        clearPending();
        pushUndo(label, undoCommands, redoCommands);
        if (m_type == QLatin1String("hash"))
            loadHash();
        else if (m_type == QLatin1String("list"))
            loadList();
        else if (m_type == QLatin1String("set"))
            loadSet();
        else if (m_type == QLatin1String("zset"))
            loadZSet();
        setStatus(tr("Changes applied"));
    });
}

void ValuePanel::discardPending()
{
    if (m_readOnly || m_pendingCommands.isEmpty())
        return;
    clearPending();
    if (m_type == QLatin1String("hash"))
        loadHash();
    else if (m_type == QLatin1String("list"))
        loadList();
    else if (m_type == QLatin1String("set"))
        loadSet();
    else if (m_type == QLatin1String("zset"))
        loadZSet();
    setStatus(tr("Pending changes discarded"));
}

void ValuePanel::updatePendingButtons()
{
    const bool pending = !m_pendingCommands.isEmpty();
    if (m_applyButton)
        m_applyButton->setEnabled(!m_readOnly && pending);
    if (m_discardButton)
        m_discardButton->setEnabled(!m_readOnly && pending);
}

void ValuePanel::clearPending()
{
    m_pendingCommands.clear();
    m_pendingUndo.clear();
    m_pendingRedo.clear();
    m_pendingLabel.clear();
    updatePendingButtons();
    updateDirtyLabel();
}

void ValuePanel::updateDirtyLabel()
{
    if (m_type == QLatin1String("string") && m_stringEdit) {
        m_stringDirty = m_stringRaw != m_stringLoadedRaw;
    } else if ((m_type == QLatin1String("ReJSON-RL") || m_type == QLatin1String("json"))
               && m_jsonEdit) {
        m_jsonDirty = m_jsonEdit->toPlainText() != m_jsonLoaded;
    }
    const bool dirty = !m_pendingCommands.isEmpty()
        || (m_type == QLatin1String("string") && m_stringDirty)
        || ((m_type == QLatin1String("ReJSON-RL") || m_type == QLatin1String("json")) && m_jsonDirty);
    if (m_dirtyLabel) {
        m_dirtyLabel->setText(tr("● Modified"));
        m_dirtyLabel->setVisible(dirty);
    }
    if (m_stringDiscardButton)
        m_stringDiscardButton->setEnabled(!m_readOnly && m_stringDirty);
    if (m_jsonDiscardButton)
        m_jsonDiscardButton->setEnabled(!m_readOnly && m_jsonDirty);
}

void ValuePanel::discardString()
{
    if (m_readOnly || m_stringPartial || !m_stringDirty)
        return;
    m_stringRaw = m_stringLoadedRaw;
    m_stringUpdating = true;
    m_stringEdit->setPlainText(m_stringLoaded);
    m_stringUpdating = false;
    m_stringDirty = false;
    updateDirtyLabel();
    setStatus(tr("String changes discarded"));
}

void ValuePanel::discardJson()
{
    if (m_readOnly || !m_jsonDirty)
        return;
    m_jsonEdit->setPlainText(m_jsonLoaded);
    m_jsonDirty = false;
    updateDirtyLabel();
    rebuildJsonTree();
    setStatus(tr("JSON changes discarded"));
}

void ValuePanel::setStatus(const QString &text)
{
    m_statusLabel->setStyleSheet(QString());
    m_statusLabel->setText(text);
}

void ValuePanel::showError(const QString &error)
{
    m_statusLabel->setStyleSheet(QStringLiteral("color: #da4453;"));
    QString message = error;
    if (error.contains(QStringLiteral("NOPERM"), Qt::CaseInsensitive)
        || error.contains(QStringLiteral("READONLY"), Qt::CaseInsensitive)
        || error.contains(QStringLiteral("MISCONF"), Qt::CaseInsensitive)) {
        message += QLatin1Char('\n')
            + tr("The server rejected this write (ACL or server read-only mode).");
    }
    m_statusLabel->setText(message);
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
