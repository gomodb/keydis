// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#pragma once

#include <QString>

namespace KeyDis {

class SecretStore
{
public:
    [[nodiscard]] bool isAvailable() const;
    [[nodiscard]] QString readPassword(const QString &profileId, QString *error = nullptr) const;
    bool writePassword(const QString &profileId, const QString &password,
                       QString *error = nullptr) const;
    bool deletePassword(const QString &profileId, QString *error = nullptr) const;

    [[nodiscard]] QString readSshPassword(const QString &profileId,
                                          QString *error = nullptr) const;
    bool writeSshPassword(const QString &profileId, const QString &password,
                          QString *error = nullptr) const;
    bool deleteSshPassword(const QString &profileId, QString *error = nullptr) const;

private:
    static QString keyFor(const QString &profileId, const QString &kind);
};

} // namespace KeyDis
