// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "core/security/secretstore.h"

#include <KWallet>

#include <memory>

namespace KeyDis {

namespace {

constexpr const char *kFolder = "keydis";

std::unique_ptr<KWallet::Wallet> openWallet(QString *error)
{
    if (!KWallet::Wallet::isEnabled()) {
        if (error)
            *error = QStringLiteral("KWallet is not enabled");
        return {};
    }
    std::unique_ptr<KWallet::Wallet> wallet(
        KWallet::Wallet::openWallet(KWallet::Wallet::NetworkWallet(), 0,
                                    KWallet::Wallet::Synchronous));
    if (!wallet || !wallet->isOpen()) {
        if (error)
            *error = QStringLiteral("Could not open KWallet");
        return {};
    }
    if (!wallet->setFolder(QString::fromLatin1(kFolder))) {
        if (!wallet->createFolder(QString::fromLatin1(kFolder))
            || !wallet->setFolder(QString::fromLatin1(kFolder))) {
            if (error)
                *error = QStringLiteral("Could not open KWallet folder");
            return {};
        }
    }
    return wallet;
}

} // namespace

QString SecretStore::keyFor(const QString &profileId)
{
    return QStringLiteral("connection/%1/password").arg(profileId);
}

bool SecretStore::isAvailable() const
{
    return KWallet::Wallet::isEnabled();
}

QString SecretStore::readPassword(const QString &profileId, QString *error) const
{
    auto wallet = openWallet(error);
    if (!wallet)
        return {};
    QString password;
    const int result = wallet->readPassword(keyFor(profileId), password);
    if (result != 0) {
        if (error)
            *error = QStringLiteral("Could not read password from KWallet");
        return {};
    }
    return password;
}

bool SecretStore::writePassword(const QString &profileId, const QString &password,
                                QString *error) const
{
    auto wallet = openWallet(error);
    if (!wallet)
        return false;
    if (wallet->writePassword(keyFor(profileId), password) != 0) {
        if (error)
            *error = QStringLiteral("Could not write password to KWallet");
        return false;
    }
    return true;
}

bool SecretStore::deletePassword(const QString &profileId, QString *error) const
{
    auto wallet = openWallet(error);
    if (!wallet)
        return false;
    wallet->removeEntry(keyFor(profileId));
    return true;
}

} // namespace KeyDis
