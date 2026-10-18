// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "core/conn/profilestore.h"

#include "core/app_paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <utility>

namespace KeyDis {

ProfileStore::ProfileStore(QString filePath)
    : m_filePath(filePath.isEmpty() ? AppPaths::configFile() : std::move(filePath))
{
}

bool ProfileStore::load(QVector<ConnectionProfile> *profiles, QString *error) const
{
    if (!profiles)
        return false;
    profiles->clear();

    QFile file(m_filePath);
    if (!file.exists())
        return true;
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Cannot read %1: %2").arg(m_filePath, file.errorString());
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = QStringLiteral("Invalid connection file: %1").arg(parseError.errorString());
        return false;
    }

    const QJsonArray array = document.object().value(QStringLiteral("profiles")).toArray();
    for (const QJsonValue &value : array) {
        if (value.isObject())
            profiles->append(ConnectionProfile::fromJson(value.toObject()));
    }
    return true;
}

bool ProfileStore::save(const QVector<ConnectionProfile> &profiles, QString *error) const
{
    QJsonArray array;
    for (const ConnectionProfile &profile : profiles)
        array.append(profile.toJson());

    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), 1);
    root.insert(QStringLiteral("profiles"), array);

    const QString directory = QFileInfo(m_filePath).absolutePath();
    if (!QDir().mkpath(directory)) {
        if (error)
            *error = QStringLiteral("Cannot create %1").arg(directory);
        return false;
    }

    const QString backup = m_filePath + QStringLiteral(".bak");
    if (QFile::exists(m_filePath)) {
        QFile::remove(backup);
        QFile::copy(m_filePath, backup);
    }

    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("Cannot write %1: %2").arg(m_filePath, file.errorString());
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("Cannot commit %1: %2").arg(m_filePath, file.errorString());
        return false;
    }
    return true;
}

QString ProfileStore::filePath() const
{
    return m_filePath;
}

} // namespace KeyDis
