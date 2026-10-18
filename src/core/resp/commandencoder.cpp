// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "core/resp/commandencoder.h"

#include <QChar>

namespace KeyDis {

namespace {

QString unescapeDoubleQuoted(QChar ch)
{
    switch (ch.unicode()) {
    case 'n':
        return QStringLiteral("\n");
    case 'r':
        return QStringLiteral("\r");
    case 't':
        return QStringLiteral("\t");
    case '\\':
        return QStringLiteral("\\");
    case '"':
        return QStringLiteral("\"");
    default:
        return QString(ch);
    }
}

} // namespace

QByteArray CommandEncoder::encode(const QList<QByteArray> &args)
{
    QByteArray out;
    out += '*';
    out += QByteArray::number(args.size());
    out += "\r\n";
    for (const QByteArray &arg : args) {
        out += '$';
        out += QByteArray::number(arg.size());
        out += "\r\n";
        out += arg;
        out += "\r\n";
    }
    return out;
}

bool CommandEncoder::parseLine(const QString &line, QList<QByteArray> *args, QString *error)
{
    if (!args) {
        if (error)
            *error = QStringLiteral("argument list is null");
        return false;
    }
    args->clear();

    QString current;
    bool hasToken = false;
    enum class Quote { None, Single, Double };
    Quote quote = Quote::None;

    for (qsizetype index = 0; index < line.size(); ++index) {
        const QChar ch = line.at(index);

        if (quote == Quote::None && ch.isSpace()) {
            if (hasToken) {
                args->append(current.toUtf8());
                current.clear();
                hasToken = false;
            }
            continue;
        }

        if (quote == Quote::None && (ch == QLatin1Char('\'') || ch == QLatin1Char('"'))) {
            quote = ch == QLatin1Char('\'') ? Quote::Single : Quote::Double;
            hasToken = true;
            continue;
        }

        if (quote == Quote::Single) {
            if (ch == QLatin1Char('\'')) {
                quote = Quote::None;
                continue;
            }
            current += ch;
            hasToken = true;
            continue;
        }

        if (quote == Quote::Double) {
            if (ch == QLatin1Char('"')) {
                quote = Quote::None;
                continue;
            }
            if (ch == QLatin1Char('\\') && index + 1 < line.size()) {
                current += unescapeDoubleQuoted(line.at(++index));
                hasToken = true;
                continue;
            }
            current += ch;
            hasToken = true;
            continue;
        }

        if (ch == QLatin1Char('\\') && index + 1 < line.size()) {
            current += line.at(++index);
            hasToken = true;
            continue;
        }

        current += ch;
        hasToken = true;
    }

    if (quote != Quote::None) {
        if (error)
            *error = QStringLiteral("unterminated quoted argument");
        return false;
    }
    if (hasToken)
        args->append(current.toUtf8());

    return true;
}

} // namespace KeyDis
