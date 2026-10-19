// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QByteArray>
#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
class QTableWidget;

namespace KeyDis {

class RedisClient;

class GeoSearchDialog : public QDialog
{
    Q_OBJECT

public:
    explicit GeoSearchDialog(RedisClient *connection, const QByteArray &key,
                             QWidget *parent = nullptr);

private:
    void search();

    RedisClient *m_connection = nullptr;
    QByteArray m_key;
    QDoubleSpinBox *m_longitude = nullptr;
    QDoubleSpinBox *m_latitude = nullptr;
    QDoubleSpinBox *m_radius = nullptr;
    QComboBox *m_unit = nullptr;
    QSpinBox *m_count = nullptr;
    QTableWidget *m_results = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace KeyDis
