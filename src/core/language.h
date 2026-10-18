// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sunhowcn
// Derived from NutSync (https://github.com/gomodb/nutsync),
// commit f7fd60730110f942911e56eefa8c6771abb3eb2a,
// originally GPL-3.0-or-later; relicensed to MIT by the copyright holder.

#pragma once

#include <QLocale>
#include <QString>

class QCoreApplication;
class QTranslator;

namespace KeyDis::Language {

QString normalizePreference(const QString &preference);
QString resolve(const QString &preference, QLocale::Language systemLanguage);
QString resolve(const QString &preference);

bool installTranslator(QCoreApplication &application, const QString &language,
                       QTranslator *applicationTranslator, QTranslator *qtTranslator = nullptr);

} // namespace KeyDis::Language
