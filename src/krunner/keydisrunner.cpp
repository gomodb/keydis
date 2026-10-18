// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "keydisrunner.h"

#include "core/conn/connectionprofile.h"
#include "core/conn/profilestore.h"

#include <KPluginFactory>
#include <QProcess>
#include <QStandardPaths>
#include <QVariantList>

using KeyDis::KeyDisRunner;
K_PLUGIN_CLASS_WITH_JSON(KeyDisRunner, "keydisrunner.json")

namespace KeyDis {

KeyDisRunner::KeyDisRunner(QObject *parent, const KPluginMetaData &metaData)
    : KRunner::AbstractRunner(parent, metaData)
{
    setObjectName(QStringLiteral("KeyDis"));
}

void KeyDisRunner::match(KRunner::RunnerContext &context)
{
    const QString query = context.query().trimmed();
    const bool exact = query.compare(QStringLiteral("redis"), Qt::CaseInsensitive) == 0
        || query.compare(QStringLiteral("keydis"), Qt::CaseInsensitive) == 0;
    if (!exact && !query.startsWith(QStringLiteral("redis "), Qt::CaseInsensitive)
        && !query.startsWith(QStringLiteral("keydis "), Qt::CaseInsensitive)) {
        return;
    }
    const QString term = exact ? QString() : query.section(QLatin1Char(' '), 1).trimmed();

    ProfileStore store;
    QVector<ConnectionProfile> profiles;
    if (!store.load(&profiles))
        return;

    for (const ConnectionProfile &profile : profiles) {
        if (!term.isEmpty() && !profile.name.contains(term, Qt::CaseInsensitive)
            && !profile.host.contains(term, Qt::CaseInsensitive)) {
            continue;
        }
        KRunner::QueryMatch match(this);
        match.setText(tr("Connect to %1 (%2:%3)").arg(profile.name, profile.host).arg(profile.port));
        match.setSubtext(profile.cluster ? tr("Redis Cluster") : tr("Redis/Valkey"));
        match.setIconName(QStringLiteral("keydis"));
        match.setRelevance(0.8);
        match.setData(QVariantList{profile.host, profile.port, profile.tls, profile.username,
                                   profile.db, profile.cluster});
        context.addMatch(match);
    }
}

void KeyDisRunner::run(const KRunner::RunnerContext &context, const KRunner::QueryMatch &match)
{
    Q_UNUSED(context)
    const QVariantList data = match.data().toList();
    if (data.size() < 6)
        return;
    QString executable = QStandardPaths::findExecutable(QStringLiteral("keydis"));
    if (executable.isEmpty())
        executable = QStringLiteral("/usr/bin/keydis");

    QStringList args{QStringLiteral("--host"), data.at(0).toString(),
                     QStringLiteral("--port"), data.at(1).toString(),
                     QStringLiteral("--db"), data.at(4).toString(),
                     QStringLiteral("--connect")};
    if (data.at(2).toBool())
        args << QStringLiteral("--tls");
    if (!data.at(3).toString().isEmpty())
        args << QStringLiteral("--username") << data.at(3).toString();
    if (data.at(5).toBool())
        args << QStringLiteral("--cluster");
    QProcess::startDetached(executable, args);
}

} // namespace KeyDis

#include "keydisrunner.moc"
