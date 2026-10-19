// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QByteArray>
#include <QDialog>

class QComboBox;
class QLabel;
class QPlainTextEdit;

namespace KeyDis {

class BinaryEditDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BinaryEditDialog(const QString &title, const QByteArray &bytes,
                              QWidget *parent = nullptr);

    [[nodiscard]] QByteArray bytes() const;

private:
    [[nodiscard]] bool currentBytes(QByteArray *bytes, QString *error) const;
    void formatCurrent();
    void updatePreview();
    void acceptIfValid();

    QComboBox *m_mode = nullptr;
    QPlainTextEdit *m_editor = nullptr;
    QLabel *m_status = nullptr;
    QByteArray m_bytes;
};

} // namespace KeyDis
