// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow.h"

#include <KAboutData>
#include <KCrash>
#include <KLocalizedString>

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    KLocalizedString::setApplicationDomain(QByteArrayLiteral("taskpilot"));

    KAboutData about(QStringLiteral("taskpilot"),
                     i18nc("@title", "TaskPilot"),
                     QStringLiteral("0.1.0"),
                     i18n("A system task manager for systemd services, timers, mounts, cron jobs and more."),
                     KAboutLicense::GPL_V3,
                     i18n("© 2026 Eric Frost"));
    about.addAuthor(i18nc("@info:credit", "Eric Frost"), i18nc("@info:credit", "Author"));
    about.setHomepage(QStringLiteral("https://github.com/eric-frost/taskpilot"));
    about.setDesktopFileName(QStringLiteral("org.kde.taskpilot"));

    KAboutData::setApplicationData(about);
    KCrash::initialize();
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("org.kde.taskpilot")));

    QCommandLineParser parser;
    about.setupCommandLine(&parser);
    parser.process(app);
    about.processCommandLine(&parser);

    auto *window = new MainWindow;
    window->show();

    return app.exec();
}
