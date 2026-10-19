// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "core/format/byteformat.h"

#include <QChar>

#include <utility>

namespace KeyDis {

QString ByteFormat::toHex(const QByteArray &bytes)
{
    QString output;
    output.reserve(bytes.size() * 3);
    for (int index = 0; index < bytes.size(); ++index) {
        if (index > 0)
            output += (index % 16 == 0) ? QLatin1Char('\n') : QLatin1Char(' ');
        output += QStringLiteral("%1")
                      .arg(static_cast<unsigned char>(bytes.at(index)), 2, 16,
                           QLatin1Char('0'))
                      .toUpper();
    }
    return output;
}

bool ByteFormat::fromHex(const QString &text, QByteArray *bytes, QString *error)
{
    if (!bytes)
        return false;
    QString clean;
    clean.reserve(text.size());
    for (const QChar character : text) {
        if (character.isSpace() || character == QLatin1Char(':'))
            continue;
        clean += character;
    }
    clean.replace(QStringLiteral("0x"), QString(), Qt::CaseInsensitive);
    clean.replace(QStringLiteral("0X"), QString(), Qt::CaseInsensitive);
    for (const QChar character : std::as_const(clean)) {
        if (!character.isDigit()
            && (character.toLower() < QLatin1Char('a')
                || character.toLower() > QLatin1Char('f'))) {
            if (error)
                *error = QStringLiteral("Invalid hexadecimal character: %1").arg(character);
            return false;
        }
    }
    if (clean.size() % 2 != 0) {
        if (error)
            *error = QStringLiteral("Hexadecimal input must contain an even number of digits");
        return false;
    }
    QByteArray result;
    result.reserve(clean.size() / 2);
    for (int index = 0; index < clean.size(); index += 2) {
        bool ok = false;
        const int value = clean.mid(index, 2).toInt(&ok, 16);
        if (!ok) {
            if (error)
                *error = QStringLiteral("Invalid hexadecimal input");
            return false;
        }
        result.append(static_cast<char>(value));
    }
    *bytes = result;
    return true;
}

QString ByteFormat::toBase64(const QByteArray &bytes)
{
    return QString::fromLatin1(bytes.toBase64());
}

bool ByteFormat::fromBase64(const QString &text, QByteArray *bytes, QString *error)
{
    if (!bytes)
        return false;
    const auto decoded = QByteArray::fromBase64Encoding(
        text.toLatin1(), QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
    if (decoded.decodingStatus != QByteArray::Base64DecodingStatus::Ok) {
        if (error)
            *error = QStringLiteral("Invalid Base64 input");
        return false;
    }
    *bytes = decoded.decoded;
    return true;
}

} // namespace KeyDis
