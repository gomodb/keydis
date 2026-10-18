// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sunhowcn
// Derived from NutSync (https://github.com/gomodb/nutsync),
// commit f7fd60730110f942911e56eefa8c6771abb3eb2a,
// originally GPL-3.0-or-later; relicensed to MIT by the copyright holder.

#include "core/logger.h"

#include "core/app_paths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QTextStream>

#include <cstdio>

namespace KeyDis {

namespace {

QMutex g_mutex;
bool g_installed = false;
QString g_level = QStringLiteral("info");
qint64 g_maxFileBytes = 2 * 1024 * 1024;
int g_retentionFiles = 3;

int levelRank(const QString &level)
{
    if (level == QLatin1String("debug"))
        return 0;
    if (level == QLatin1String("warning"))
        return 2;
    if (level == QLatin1String("error"))
        return 3;
    return 1;
}

bool shouldLog(int messageRank)
{
    QMutexLocker locker(&g_mutex);
    return messageRank >= levelRank(g_level);
}

void rotateIfNeeded(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() || info.size() <= g_maxFileBytes)
        return;

    QFile::remove(path + QStringLiteral(".%1").arg(g_retentionFiles));
    for (int index = g_retentionFiles - 1; index >= 1; --index) {
        const QString source = path + QStringLiteral(".%1").arg(index);
        if (QFile::exists(source))
            QFile::rename(source, path + QStringLiteral(".%1").arg(index + 1));
    }
    QFile::rename(path, path + QStringLiteral(".1"));
}

void writeLine(const QString &line)
{
    QMutexLocker locker(&g_mutex);
    const QString path = AppPaths::logFile();
    rotateIfNeeded(path);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    QTextStream stream(&file);
    stream << line << '\n';
}

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    const char *level = "INFO";
    switch (type) {
    case QtDebugMsg:
        level = "DEBUG";
        break;
    case QtInfoMsg:
        level = "INFO";
        break;
    case QtWarningMsg:
        level = "WARN";
        break;
    case QtCriticalMsg:
        level = "ERROR";
        break;
    case QtFatalMsg:
        level = "FATAL";
        break;
    }

    const int messageRank = type == QtDebugMsg ? 0
        : type == QtInfoMsg                    ? 1
        : type == QtWarningMsg                 ? 2
                                               : 3;
    if (!shouldLog(messageRank)) {
        if (type == QtFatalMsg)
            std::abort();
        return;
    }

    const QString category =
        context.category ? QString::fromLatin1(context.category) : QStringLiteral("default");
    const QString line = QStringLiteral("%1 [%2] %3: %4")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                                  QString::fromLatin1(level), category, Logger::redact(message));

    std::fprintf(stderr, "%s\n", line.toLocal8Bit().constData());
    writeLine(line);

    if (type == QtFatalMsg)
        std::abort();
}

} // namespace

void Logger::install()
{
    if (g_installed)
        return;
    AppPaths::ensureDirectories();
    qInstallMessageHandler(messageHandler);
    g_installed = true;
    qInfo() << "KeyDis started, log file:" << AppPaths::logFile();
}

void Logger::setRetention(int maxSizeMb, int files)
{
    QMutexLocker locker(&g_mutex);
    g_maxFileBytes = qBound(1, maxSizeMb, 100) * 1024LL * 1024LL;
    g_retentionFiles = qBound(1, files, 10);
}

void Logger::setLevel(const QString &level)
{
    QMutexLocker locker(&g_mutex);
    g_level = (level == QLatin1String("debug") || level == QLatin1String("warning")
               || level == QLatin1String("error"))
        ? level
        : QStringLiteral("info");
}

QString Logger::level()
{
    QMutexLocker locker(&g_mutex);
    return g_level;
}

void Logger::log(const QString &category, const QString &message)
{
    if (!shouldLog(1))
        return;
    writeLine(QStringLiteral("%1 [UI] %2: %3")
                  .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs), category,
                       redact(message)));
}

QString Logger::logFilePath()
{
    return AppPaths::logFile();
}

QString Logger::redact(const QString &message)
{
    QString redacted = message;

    // Never keep credentials in logs. This is intentionally conservative:
    // matching more than necessary is better than leaking a password.
    static const QRegularExpression authPattern(
        QStringLiteral(R"((?i)\b(AUTH|HELLO\s+3\s+AUTH)\b[^\r\n]*)"));
    redacted.replace(authPattern, QStringLiteral("\\1 ***"));

    static const QRegularExpression secretPattern(
        QStringLiteral(R"((?i)\b(password|passwd|secret|token|private[_-]?key)\s*[=:]\s*\S+)"));
    redacted.replace(secretPattern, QStringLiteral("\\1=***"));

    return redacted;
}

} // namespace KeyDis
