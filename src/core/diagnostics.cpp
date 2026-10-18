// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sunhowcn
// Derived from NutSync (https://github.com/gomodb/nutsync),
// commit f7fd60730110f942911e56eefa8c6771abb3eb2a,
// originally GPL-3.0-or-later; relicensed to MIT by the copyright holder.

#include "core/diagnostics.h"

#include "core/app_paths.h"
#include "core/logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QSysInfo>
#include <QTextStream>

namespace KeyDis::Diagnostics {

namespace {

QString redactedLocalPath(const QString &path)
{
    const QString home = QDir::homePath();
    if (!home.isEmpty() && path.startsWith(home))
        return QStringLiteral("~") + path.mid(home.size());
    return path;
}

} // namespace

QString buildReport()
{
    QString report;
    QTextStream out(&report);
    out << "KeyDis diagnostics\n";
    out << "Generated: " << QDateTime::currentDateTime().toString(Qt::ISODate) << '\n';
    out << "KeyDis: " << QString::fromLatin1(KEYDIS_VERSION) << '\n';
    out << "Qt: " << QString::fromLatin1(qVersion()) << '\n';
    out << "OS: " << QSysInfo::prettyProductName() << '\n';
    out << "Kernel: " << QSysInfo::kernelType() << ' ' << QSysInfo::kernelVersion() << '\n';
    out << "Architecture: " << QSysInfo::currentCpuArchitecture() << '\n';
    out << "Locale: " << QLocale::system().name() << '\n';
    out << "Config: " << redactedLocalPath(AppPaths::configDir()) << '\n';
    out << "State: " << redactedLocalPath(AppPaths::stateDir()) << '\n';
    out << "Log: " << redactedLocalPath(Logger::logFilePath()) << '\n';
    out << '\n';

    out << "Secrets and key/value data are intentionally omitted.\n";
    return report;
}

QString readLogTail(const QString &logPath, int maxLines)
{
    QFile file(logPath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const qint64 size = file.size();
    const qint64 start = qMax<qint64>(0, size - 64 * 1024);
    if (!file.seek(start))
        return {};
    QString text = QString::fromUtf8(file.readAll());
    QStringList lines = text.split(QLatin1Char('\n'));
    if (start > 0 && !lines.isEmpty())
        lines.removeFirst();
    if (lines.size() > maxLines)
        lines = lines.mid(lines.size() - maxLines);
    return lines.join(QLatin1Char('\n'));
}

} // namespace KeyDis::Diagnostics
