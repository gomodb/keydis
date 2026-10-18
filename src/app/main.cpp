// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KeyDis contributors

#include "app/mainwindow.h"
#include "core/app_paths.h"
#include "core/language.h"
#include "core/logger.h"

#include <KAboutData>
#include <KConfigGroup>
#include <KDBusService>
#include <KLocalizedString>
#include <KSharedConfig>

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QPointer>
#include <QTimer>
#include <QTranslator>

#include <memory>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);

    QApplication::setApplicationName(QStringLiteral("keydis"));
    QApplication::setApplicationDisplayName(QStringLiteral("KeyDis"));
    QApplication::setApplicationVersion(QStringLiteral(KEYDIS_VERSION));
    QApplication::setOrganizationName(QStringLiteral("KeyDis"));
    QApplication::setOrganizationDomain(QStringLiteral("keydis.github.io"));
    QApplication::setDesktopFileName(QStringLiteral("io.github.keydis.KeyDis"));
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("keydis"),
                                                  QIcon(QStringLiteral(":/icons/keydis.svg"))));

    KLocalizedString::setApplicationDomain("keydis");

    KAboutData about(QStringLiteral("keydis"), QStringLiteral("KeyDis"),
                     QStringLiteral(KEYDIS_VERSION),
                     QStringLiteral("Native Redis/Valkey client for KDE"),
                     KAboutLicense::MIT,
                     QStringLiteral("Copyright (c) 2026 KeyDis contributors"),
                     QStringLiteral("Portions derived from NutSync, Copyright (c) 2026 sunhowcn."),
                     QStringLiteral("https://github.com/gomodb/keydis"),
                     QStringLiteral("https://github.com/gomodb/keydis/issues"));
    KAboutData::setApplicationData(about);

    QCommandLineParser parser;
    about.setupCommandLine(&parser);
    const QCommandLineOption hostOption(QStringLiteral("host"),
                                        QStringLiteral("Redis/Valkey host"),
                                        QStringLiteral("host"));
    const QCommandLineOption portOption(QStringLiteral("port"),
                                        QStringLiteral("Redis/Valkey port"),
                                        QStringLiteral("port"), QStringLiteral("6379"));
    const QCommandLineOption tlsOption(QStringLiteral("tls"),
                                       QStringLiteral("Use TLS"));
    const QCommandLineOption usernameOption(QStringLiteral("username"),
                                            QStringLiteral("ACL username"),
                                            QStringLiteral("username"));
    const QCommandLineOption passwordOption(QStringLiteral("password"),
                                            QStringLiteral("Password (avoid on shared machines)"),
                                            QStringLiteral("password"));
    const QCommandLineOption dbOption(QStringLiteral("db"), QStringLiteral("Database number"),
                                      QStringLiteral("db"), QStringLiteral("0"));
    const QCommandLineOption connectOption(QStringLiteral("connect"),
                                           QStringLiteral("Connect immediately on startup"));
    const QCommandLineOption quitAfterOption(QStringLiteral("quit-after"),
                                             QStringLiteral("Quit after N milliseconds (testing)"),
                                             QStringLiteral("ms"));
    const QCommandLineOption closeAfterOption(QStringLiteral("close-after"),
                                              QStringLiteral("Close window after N milliseconds (testing)"),
                                              QStringLiteral("ms"));
    const QCommandLineOption tabOption(QStringLiteral("tab"),
                                       QStringLiteral("Initial tab: dashboard, browser, cli"),
                                       QStringLiteral("name"));
    const QCommandLineOption clusterOption(QStringLiteral("cluster"),
                                           QStringLiteral("Connect using Redis Cluster mode"));
    parser.addOption(hostOption);
    parser.addOption(portOption);
    parser.addOption(tlsOption);
    parser.addOption(usernameOption);
    parser.addOption(passwordOption);
    parser.addOption(dbOption);
    parser.addOption(connectOption);
    parser.addOption(quitAfterOption);
    parser.addOption(closeAfterOption);
    parser.addOption(tabOption);
    parser.addOption(clusterOption);
    parser.process(application);
    about.processCommandLine(&parser);

    const KConfigGroup general(KSharedConfig::openConfig(), QStringLiteral("General"));
    const QString languagePreference = general.readEntry("language", QStringLiteral("system"));
    const QString language = KeyDis::Language::resolve(languagePreference);

    QTranslator applicationTranslator;
    QTranslator qtTranslator;
    if (!KeyDis::Language::installTranslator(application, language, &applicationTranslator,
                                             &qtTranslator)
        && language != QLatin1String("en")) {
        // Translation resources are added in a later P0 step; English source
        // strings remain usable if the catalog is missing.
        qWarning() << "KeyDis: translation catalog not found for" << language;
    }

    KeyDis::Logger::install();
    KeyDis::Logger::setLevel(general.readEntry("logLevel", QStringLiteral("info")));
    KeyDis::Logger::setRetention(general.readEntry("logMaxSizeMb", 2),
                                 general.readEntry("logRetentionFiles", 3));
    KeyDis::AppPaths::ensureDirectories();

    std::unique_ptr<KDBusService> service;
    if (qEnvironmentVariableIsEmpty("KEYDIS_NO_DBUS"))
        service = std::make_unique<KDBusService>(KDBusService::Unique);

    // KXmlGuiWindow has Qt::WA_DeleteOnClose set by default, so it must live
    // on the heap. QPointer lets us clean it up if the app quits without a
    // close event.
    QPointer<KeyDis::MainWindow> window = new KeyDis::MainWindow;
    if (parser.isSet(hostOption) || parser.isSet(portOption) || parser.isSet(tlsOption)
        || parser.isSet(usernameOption) || parser.isSet(passwordOption) || parser.isSet(dbOption)) {
        bool portOk = false;
        const int port = parser.value(portOption).toInt(&portOk);
        window->setConnectionParameters(parser.value(hostOption),
                                        static_cast<quint16>(portOk && port > 0 ? port : 6379),
                                        parser.isSet(tlsOption), parser.value(usernameOption),
                                        parser.value(passwordOption),
                                        parser.value(dbOption).toInt());
    }
    window->show();
    if (parser.isSet(clusterOption))
        window->setClusterMode(true);
    if (parser.isSet(tabOption))
        window->showTab(parser.value(tabOption));
    if (parser.isSet(connectOption))
        QTimer::singleShot(0, window, &KeyDis::MainWindow::connectNow);
    if (parser.isSet(quitAfterOption))
        QTimer::singleShot(parser.value(quitAfterOption).toInt(), &application,
                           &QCoreApplication::quit);
    if (parser.isSet(closeAfterOption)) {
        QTimer::singleShot(parser.value(closeAfterOption).toInt(), window, [window] {
            if (window)
                window->close();
        });
    }
    const int result = application.exec();
    if (window)
        delete window;
    return result;
}
