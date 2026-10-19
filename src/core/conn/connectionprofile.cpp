// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "core/conn/connectionprofile.h"

#include <QUuid>

namespace KeyDis {

QJsonObject ConnectionProfile::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("group"), group);
    object.insert(QStringLiteral("color"), color);
    object.insert(QStringLiteral("host"), host);
    object.insert(QStringLiteral("port"), port);
    object.insert(QStringLiteral("tls"), tls);
    object.insert(QStringLiteral("tlsCaFile"), tlsCaFile);
    object.insert(QStringLiteral("tlsCertFile"), tlsCertFile);
    object.insert(QStringLiteral("tlsKeyFile"), tlsKeyFile);
    object.insert(QStringLiteral("tlsVerifyPeer"), tlsVerifyPeer);
    object.insert(QStringLiteral("username"), username);
    object.insert(QStringLiteral("db"), db);
    object.insert(QStringLiteral("readOnly"), readOnly);
    object.insert(QStringLiteral("cluster"), cluster);
    object.insert(QStringLiteral("sentinel"), sentinel);
    object.insert(QStringLiteral("sentinelMaster"), sentinelMaster);
    object.insert(QStringLiteral("sentinelHost"), sentinelHost);
    object.insert(QStringLiteral("sentinelPort"), sentinelPort);
    object.insert(QStringLiteral("sentinelTls"), sentinelTls);
    object.insert(QStringLiteral("sshTunnel"), sshTunnel);
    object.insert(QStringLiteral("sshHost"), sshHost);
    object.insert(QStringLiteral("sshPort"), sshPort);
    object.insert(QStringLiteral("sshUser"), sshUser);
    object.insert(QStringLiteral("sshKeyPath"), sshKeyPath);
    return object;
}

ConnectionProfile ConnectionProfile::fromJson(const QJsonObject &object)
{
    ConnectionProfile profile;
    profile.id = object.value(QStringLiteral("id")).toString();
    if (profile.id.isEmpty())
        profile.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    profile.name = object.value(QStringLiteral("name")).toString(profile.name);
    profile.group = object.value(QStringLiteral("group")).toString(profile.group);
    profile.color = object.value(QStringLiteral("color")).toString();
    profile.host = object.value(QStringLiteral("host")).toString(profile.host);
    const int port = object.value(QStringLiteral("port")).toInt(profile.port);
    profile.port = static_cast<quint16>(qBound(1, port, 65535));
    profile.tls = object.value(QStringLiteral("tls")).toBool(false);
    profile.tlsCaFile = object.value(QStringLiteral("tlsCaFile")).toString();
    profile.tlsCertFile = object.value(QStringLiteral("tlsCertFile")).toString();
    profile.tlsKeyFile = object.value(QStringLiteral("tlsKeyFile")).toString();
    profile.tlsVerifyPeer = object.value(QStringLiteral("tlsVerifyPeer")).toBool(true);
    profile.username = object.value(QStringLiteral("username")).toString();
    profile.db = qBound(0, object.value(QStringLiteral("db")).toInt(0), 15);
    profile.readOnly = object.value(QStringLiteral("readOnly")).toBool(false);
    profile.cluster = object.value(QStringLiteral("cluster")).toBool(false);
    profile.sentinel = object.value(QStringLiteral("sentinel")).toBool(false);
    profile.sentinelMaster = object.value(QStringLiteral("sentinelMaster")).toString();
    profile.sentinelHost = object.value(QStringLiteral("sentinelHost")).toString();
    profile.sentinelPort = static_cast<quint16>(object.value(QStringLiteral("sentinelPort"))
                                                    .toInt(26379));
    profile.sentinelTls = object.value(QStringLiteral("sentinelTls")).toBool(false);
    profile.sshTunnel = object.value(QStringLiteral("sshTunnel")).toBool(false);
    profile.sshHost = object.value(QStringLiteral("sshHost")).toString();
    profile.sshPort = static_cast<quint16>(object.value(QStringLiteral("sshPort")).toInt(22));
    profile.sshUser = object.value(QStringLiteral("sshUser")).toString();
    profile.sshKeyPath = object.value(QStringLiteral("sshKeyPath")).toString();
    return profile;
}

} // namespace KeyDis
