// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "ui/dialogs/geosearchdialog.h"

#include "core/net/redisclient.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace KeyDis {

GeoSearchDialog::GeoSearchDialog(RedisClient *connection, const QByteArray &key, QWidget *parent)
    : QDialog(parent)
    , m_connection(connection)
    , m_key(key)
{
    setWindowTitle(tr("Geo search: %1").arg(QString::fromUtf8(key)));
    resize(720, 520);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    m_longitude = new QDoubleSpinBox(this);
    m_longitude->setObjectName(QStringLiteral("geoLongitude"));
    m_longitude->setRange(-180.0, 180.0);
    m_longitude->setDecimals(6);
    m_longitude->setValue(0.0);
    form->addRow(tr("Longitude"), m_longitude);
    m_latitude = new QDoubleSpinBox(this);
    m_latitude->setObjectName(QStringLiteral("geoLatitude"));
    m_latitude->setRange(-85.05112878, 85.05112878);
    m_latitude->setDecimals(6);
    m_latitude->setValue(0.0);
    form->addRow(tr("Latitude"), m_latitude);
    m_radius = new QDoubleSpinBox(this);
    m_radius->setObjectName(QStringLiteral("geoRadius"));
    m_radius->setRange(0.001, 100000.0);
    m_radius->setDecimals(2);
    m_radius->setValue(100.0);
    form->addRow(tr("Radius"), m_radius);
    m_unit = new QComboBox(this);
    m_unit->setObjectName(QStringLiteral("geoUnit"));
    m_unit->addItem(QStringLiteral("m"), QStringLiteral("m"));
    m_unit->addItem(QStringLiteral("km"), QStringLiteral("km"));
    m_unit->addItem(QStringLiteral("mi"), QStringLiteral("mi"));
    m_unit->addItem(QStringLiteral("ft"), QStringLiteral("ft"));
    m_unit->setCurrentIndex(1);
    form->addRow(tr("Unit"), m_unit);
    m_count = new QSpinBox(this);
    m_count->setObjectName(QStringLiteral("geoCount"));
    m_count->setRange(1, 10000);
    m_count->setValue(100);
    form->addRow(tr("Max results"), m_count);
    layout->addLayout(form);

    auto *searchButton = new QPushButton(tr("Search"), this);
    searchButton->setObjectName(QStringLiteral("geoSearchButton"));
    layout->addWidget(searchButton);

    m_results = new QTableWidget(0, 4, this);
    m_results->setObjectName(QStringLiteral("geoResults"));
    m_results->setHorizontalHeaderLabels({tr("Member"), tr("Distance"), tr("Longitude"),
                                          tr("Latitude")});
    m_results->horizontalHeader()->setStretchLastSection(true);
    m_results->verticalHeader()->setVisible(false);
    m_results->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_results->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(m_results, 1);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("geoStatus"));
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_status);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(searchButton, &QPushButton::clicked, this, &GeoSearchDialog::search);
}

void GeoSearchDialog::search()
{
    if (!m_connection)
        return;
    m_status->setText(tr("Searching..."));
    m_results->setRowCount(0);
    const QString unit = m_unit->currentData().toString();
    const QList<QByteArray> command{
        QByteArrayLiteral("GEOSEARCH"), m_key, QByteArrayLiteral("FROMLONLAT"),
        QByteArray::number(m_longitude->value(), 'f', 6),
        QByteArray::number(m_latitude->value(), 'f', 6),
        QByteArrayLiteral("BYRADIUS"), QByteArray::number(m_radius->value(), 'f', 2),
        unit.toUtf8(), QByteArrayLiteral("ASC"), QByteArrayLiteral("COUNT"),
        QByteArray::number(m_count->value()), QByteArrayLiteral("WITHDIST"),
        QByteArrayLiteral("WITHCOORD")};
    m_connection->sendCommand(
        command, [this, unit](const RespValue &reply, const QString &error) {
            if (!error.isEmpty() || reply.isError()) {
                m_status->setText(error.isEmpty() ? QString::fromUtf8(reply.scalar) : error);
                return;
            }
            if (reply.type != RespType::Array) {
                m_status->setText(tr("Unexpected GEOSEARCH reply"));
                return;
            }
            const auto numeric = [](const RespValue &value) {
                if (value.type == RespType::Double)
                    return value.real;
                if (value.type == RespType::Integer)
                    return static_cast<double>(value.integer);
                return value.scalar.toDouble();
            };
            for (const RespValue &entry : reply.elements) {
                if (entry.type != RespType::Array || entry.elements.empty())
                    continue;
                const QString member = QString::fromUtf8(entry.elements[0].scalar);
                QString distance;
                QString longitude;
                QString latitude;
                for (size_t index = 1; index < entry.elements.size(); ++index) {
                    const RespValue &part = entry.elements[index];
                    if (part.type == RespType::Array && part.elements.size() == 2) {
                        longitude = QString::number(numeric(part.elements[0]), 'f', 6);
                        latitude = QString::number(numeric(part.elements[1]), 'f', 6);
                    } else if (part.type == RespType::Double
                               || part.type == RespType::Integer
                               || part.type == RespType::BulkString) {
                        if (distance.isEmpty())
                            distance = QString::number(numeric(part), 'f', 2)
                                + QLatin1Char(' ') + unit;
                    }
                }
                const int row = m_results->rowCount();
                m_results->insertRow(row);
                m_results->setItem(row, 0, new QTableWidgetItem(member));
                m_results->setItem(row, 1, new QTableWidgetItem(distance));
                m_results->setItem(row, 2, new QTableWidgetItem(longitude));
                m_results->setItem(row, 3, new QTableWidgetItem(latitude));
            }
            m_status->setText(tr("%1 results").arg(m_results->rowCount()));
        });
}

} // namespace KeyDis
