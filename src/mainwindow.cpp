// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow.h"

#include "editors/autostartedit.h"
#include "editors/bindlnedit.h"
#include "editors/cronedit.h"
#include "editors/unitcreator.h"
#include "fetchers.h"
#include "journaltab.h"
#include "services/servicestab.h"
#include "services/unitactions.h"
#include "util/auth.h"
#include "util/commandtab.h"
#include "util/dialogs.h"
#include "util/proc.h"

#include <KActionCollection>
#include <KLocalizedString>
#include <KMessageBox>
#include <KStandardAction>
#include <KStandardGuiItem>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QHash>
#include <QIcon>
#include <QProcess>
#include <QSet>
#include <QTabWidget>

#include <functional>
#include <memory>

namespace {

/*┌──────────────────────╮
  | Small helpers        |
  └──────────────────────╯*/

// The single selected row, or an empty list (after telling the user) if none.
QStringList firstSelected(QWidget *parent, CommandTab *tab)
{
    const QVector<QStringList> rows = tab->selectedRows();
    if (rows.isEmpty()) {
        KMessageBox::information(parent, i18n("Please select a row first."));
        return {};
    }
    return rows.first();
}

bool confirm(QWidget *parent, const QString &text, const KGuiItem &primary)
{
    return KMessageBox::questionTwoActions(parent, text, QString(), primary,
                                           KStandardGuiItem::cancel())
        == KMessageBox::PrimaryAction;
}

/*┌──────────────────────╮
  | Containers           |
  └──────────────────────╯*/

// Engine-aware container details. lxd's client is `lxc`; lxd/incus are instance
// managers that don't use `logs`/`inspect`.
QString containerCli(const QString &runtime)
{
    return runtime == QLatin1String("lxd") ? QStringLiteral("lxc") : runtime;
}
bool isInstanceManager(const QString &runtime)
{
    return runtime == QLatin1String("lxd") || runtime == QLatin1String("incus");
}

// Start/stop/restart a container row [Name, Runtime, …].
CommandTab::RowAction containerAction(const QString &text, const QString &icon, const QString &verb)
{
    return {text, icon, [verb](const QStringList &row) {
        if (row.size() < 2)
            return;
        runCmd(containerCli(row.at(1)), {verb, row.at(0)}, 15000);
    }};
}

void wireContainers(QWidget *parent, CommandTab *tab)
{
    tab->addRowAction(containerAction(i18nc("@action", "Start"),
        QStringLiteral("media-playback-start"), QStringLiteral("start")));
    tab->addRowAction(containerAction(i18nc("@action", "Stop"),
        QStringLiteral("media-playback-stop"), QStringLiteral("stop")));
    tab->addRowAction(containerAction(i18nc("@action", "Restart"),
        QStringLiteral("view-refresh"), QStringLiteral("restart")));

    tab->addMenuAction(i18nc("@action", "Logs"), QStringLiteral("text-x-log"), [parent, tab] {
        const QStringList row = firstSelected(parent, tab);
        if (row.size() < 2)
            return;
        const QString name = row.at(0), rt = row.at(1), cli = containerCli(rt);
        const QString text = isInstanceManager(rt)
            ? runCmd(cli, {QStringLiteral("info"), QStringLiteral("--show-log"), name}, 12000)
            : runCmd(cli, {QStringLiteral("logs"), QStringLiteral("--tail"), QStringLiteral("500"), name}, 12000);
        Dialogs::showText(parent, i18nc("@title:window", "Logs: %1", name), text, true);
    });

    tab->addMenuAction(i18nc("@action", "Inspect"), QStringLiteral("document-properties"), [parent, tab] {
        const QStringList row = firstSelected(parent, tab);
        if (row.size() < 2)
            return;
        const QString name = row.at(0), rt = row.at(1), cli = containerCli(rt);
        const QString text = isInstanceManager(rt)
            ? runCmd(cli, {QStringLiteral("config"), QStringLiteral("show"), QStringLiteral("--expanded"), name}, 12000)
            : runCmd(cli, {QStringLiteral("inspect"), name}, 12000);
        Dialogs::showText(parent, i18nc("@title:window", "Inspect: %1", name), text);
    });

    tab->addMenuAction(i18nc("@action", "Shell"), QStringLiteral("utilities-terminal"), [parent, tab] {
        const QStringList row = firstSelected(parent, tab);
        if (row.size() < 2)
            return;
        const QString name = row.at(0), rt = row.at(1), cli = containerCli(rt);
        const QString term = haveCmd(QStringLiteral("konsole"))
            ? QStringLiteral("konsole") : QStringLiteral("x-terminal-emulator");
        const QStringList args = isInstanceManager(rt)
            ? QStringList{QStringLiteral("-e"), cli, QStringLiteral("exec"), name, QStringLiteral("--"), QStringLiteral("bash")}
            : QStringList{QStringLiteral("-e"), cli, QStringLiteral("exec"), QStringLiteral("-it"), name, QStringLiteral("bash")};
        QProcess::startDetached(term, args);
    });

    tab->addMenuAction(i18nc("@action", "Remove"), QStringLiteral("edit-delete"), [parent, tab] {
        const QStringList row = firstSelected(parent, tab);
        if (row.size() < 2)
            return;
        if (!confirm(parent, i18n("Remove container \"%1\"?", row.value(0)), KStandardGuiItem::del()))
            return;
        const QString name = row.at(0), rt = row.at(1), cli = containerCli(rt);
        runCmd(cli, {isInstanceManager(rt) ? QStringLiteral("delete") : QStringLiteral("rm"), name}, 15000);
        tab->refresh();
    });

    // Show Docker (root): merge root-owned docker/podman containers (via KAuth)
    // with the unprivileged list, so docker shows without docker-group membership.
    tab->addBarAction(i18nc("@action", "Show Docker (root)"), QStringLiteral("security-high"), [parent, tab] {
        QString error;
        Auth::dockerList(&error); // probe → polkit prompt
        if (!error.isEmpty()) {
            KMessageBox::error(parent, error, i18nc("@title:window", "Elevation Failed"));
            return;
        }
        tab->setFetcher([] {
            QVector<QStringList> rows = Fetchers::containers();
            QSet<QString> ids;
            for (const QStringList &r : rows)
                if (!r.value(6).isEmpty()) // incus rows have no ID — don't poison the set
                    ids.insert(r.value(6));
            for (const QStringList &r : Auth::dockerList())
                if (r.value(6).isEmpty() || !ids.contains(r.value(6)))
                    rows.append(r);
            return rows;
        });
        tab->refresh();
    });
}

/*┌──────────────────────╮
  | Timers               |
  └──────────────────────╯*/

// Timers row is [Timer, Status, Scope, …]; id = col 0, scope = col 2.
void wireTimers(QWidget *parent, CommandTab *tab)
{
    tab->addBarAction(i18nc("@action", "Add"), QStringLiteral("list-add"),
                      [parent] { UnitCreatorDialog(parent, QStringLiteral("timer")).exec(); });

    auto life = [parent](const QString &text, const QString &icon, const QString &verb) {
        return CommandTab::RowAction{text, icon, [parent, verb](const QStringList &row) {
            if (row.size() < 3)
                return;
            QString error;
            if (!UnitActions::lifecycle(verb, row.at(0), row.at(2), &error) && !error.isEmpty())
                KMessageBox::error(parent, error, i18nc("@title:window", "Action Failed"));
        }};
    };
    tab->addRowAction(life(i18nc("@action", "Start"), QStringLiteral("media-playback-start"), QStringLiteral("start")));
    tab->addRowAction(life(i18nc("@action", "Stop"), QStringLiteral("media-playback-stop"), QStringLiteral("stop")));
    tab->addRowAction(life(i18nc("@action", "Restart"), QStringLiteral("system-reboot"), QStringLiteral("restart")));

    auto viewer = [parent, tab](void (*fn)(QWidget *, const QString &, const QString &)) {
        const QStringList row = firstSelected(parent, tab);
        if (row.size() >= 3)
            fn(parent, row.at(0), row.at(2));
    };
    tab->addMenuAction(i18nc("@action", "Logs"), QStringLiteral("view-list-text"),
                       [viewer] { viewer(&UnitActions::logs); });
    tab->addMenuAction(i18nc("@action", "Dependencies"), QStringLiteral("view-list-tree"),
                       [viewer] { viewer(&UnitActions::dependencies); });
    tab->addMenuAction(i18nc("@action", "Edit Unit File(s)"), QStringLiteral("document-edit"),
                       [viewer] { viewer(&UnitActions::editFile); });
    tab->addMenuAction(i18nc("@action", "Open File Location"), QStringLiteral("folder-open"),
                       [viewer] { viewer(&UnitActions::openLocation); });
}

/*┌──────────────────────╮
  | Cron                 |
  └──────────────────────╯*/

// Cron row is [User, Min, Hour, DoM, Month, DoW, Command].
Cron::Job cronJobFromRow(const QStringList &r)
{
    return {r.value(1), r.value(2), r.value(3), r.value(4), r.value(5), r.value(6), QString()};
}

void wireCron(QWidget *parent, CommandTab *tab)
{
    auto *userCombo = new QComboBox(parent);
    userCombo->setToolTip(i18n("Crontab to view and edit"));

    // Selected user, read on the UI thread by the action lambdas.
    auto selected = std::make_shared<QString>(Cron::currentUser());
    // Bake the user in by value so the worker-thread fetcher never reads the
    // shared (mutating) QString — avoids a cross-thread data race.
    auto repoint = [tab, selected] {
        const QString user = *selected;
        tab->setFetcher([user] { return Fetchers::cronFor(user); });
        tab->refresh();
    };

    // Populate the combo: current user, plus (after elevation) "All" + each user.
    auto populate = [userCombo](bool elevated) {
        userCombo->blockSignals(true);
        const QString keep = userCombo->currentData().toString();
        userCombo->clear();
        userCombo->addItem(i18nc("@item current user", "%1 (you)", Cron::currentUser()),
                           Cron::currentUser());
        if (elevated) {
            const QVector<Auth::CronUser> users = Auth::cronUsers(nullptr);
            int total = 0;
            for (const Auth::CronUser &u : users)
                total += u.count;
            userCombo->addItem(i18nc("@item all crontabs", "All (%1)", total), Cron::kAllUsers);
            for (const Auth::CronUser &u : users) {
                if (u.user == Cron::currentUser())
                    continue;
                userCombo->addItem(i18nc("@item user with N jobs", "%1 (%2)", u.user, u.count), u.user);
            }
        }
        const int idx = userCombo->findData(keep);
        userCombo->setCurrentIndex(idx >= 0 ? idx : 0);
        userCombo->blockSignals(false);
    };
    populate(false);

    // Add (shared by the +Add toolbar button and the right-click menu).
    auto addJob = [parent, tab, selected, userCombo] {
        // In the "All" view, let the user pick whose crontab to add to.
        QStringList users;
        if (*selected == Cron::kAllUsers)
            for (int i = 0; i < userCombo->count(); ++i) {
                const QString u = userCombo->itemData(i).toString();
                if (u != Cron::kAllUsers && !u.isEmpty())
                    users << u;
            }
        CronEditDialog dlg(parent, {}, users, Cron::currentUser());
        if (dlg.exec() != QDialog::Accepted)
            return;
        const QString user = users.isEmpty() ? *selected : dlg.user();
        QString error;
        if (!Cron::add(user, dlg.job(), &error))
            KMessageBox::error(parent, error);
        tab->refresh();
    };
    tab->addBarAction(i18nc("@action", "Add"), QStringLiteral("list-add"), addJob);

    // Elevate: fetch the user list as root, then enable the other-user views.
    tab->addBarAction(i18nc("@action", "Elevate"), QStringLiteral("security-high"),
                      [parent, populate, repoint] {
        QString error;
        Auth::cronUsers(&error); // probe → polkit prompt
        if (!error.isEmpty()) {
            KMessageBox::error(parent, error, i18nc("@title:window", "Elevation Failed"));
            return;
        }
        populate(true);
        repoint();
    }, i18nc("@info:tooltip", "Enter your password to view and edit other users' crontabs."));

    tab->addBarWidget(userCombo);
    QObject::connect(userCombo, &QComboBox::currentIndexChanged, tab,
                     [userCombo, selected, repoint](int) {
        *selected = userCombo->currentData().toString();
        repoint();
    });

    // In "All" view, CRUD targets each row's own User column; otherwise the combo user.
    auto targetUser = [selected](const QStringList &row) -> QString {
        return *selected == Cron::kAllUsers ? row.value(0) : *selected;
    };

    tab->addMenuAction(i18nc("@action", "Add"), QStringLiteral("list-add"), addJob);

    tab->addMenuAction(i18nc("@action", "Edit"), QStringLiteral("document-edit"), [parent, tab, targetUser] {
        const QStringList row = firstSelected(parent, tab);
        if (row.isEmpty())
            return;
        const QString user = targetUser(row);
        const Cron::Job match = cronJobFromRow(row);
        CronEditDialog dlg(parent, Cron::find(user, match).value_or(match));
        if (dlg.exec() != QDialog::Accepted)
            return;
        QString error;
        if (!Cron::update(user, match, dlg.job(), &error))
            KMessageBox::error(parent, error);
        tab->refresh();
    });

    tab->addMenuAction(i18nc("@action", "Delete"), QStringLiteral("list-remove"), [parent, tab, targetUser] {
        const QVector<QStringList> rows = tab->selectedRows();
        if (rows.isEmpty()) {
            KMessageBox::information(parent, i18n("Please select a row first."));
            return;
        }
        if (!confirm(parent, i18np("Delete the selected cron job?",
                                   "Delete the %1 selected cron jobs?", rows.size()),
                     KStandardGuiItem::del()))
            return;
        // Rows may span users in "All" view — group by target user, one write each.
        QHash<QString, QList<Cron::Job>> byUser;
        for (const QStringList &r : rows)
            byUser[targetUser(r)] << cronJobFromRow(r);
        QString error;
        for (auto it = byUser.constBegin(); it != byUser.constEnd(); ++it)
            if (!Cron::remove(it.key(), it.value(), &error))
                KMessageBox::error(parent, error);
        tab->refresh();
    });

    tab->addMenuAction(i18nc("@action", "Run Now"), QStringLiteral("media-playback-start"), [parent, tab] {
        const QStringList row = firstSelected(parent, tab);
        const QString cmd = row.value(6);
        if (cmd.isEmpty())
            return;
        // Run the command now (current user's shell), capturing stdout+stderr.
        const QString out = runCmd(QStringLiteral("bash"),
                                   {QStringLiteral("-c"), cmd + QStringLiteral(" 2>&1")}, 30000);
        Dialogs::showText(parent, i18nc("@title:window", "Run Now"),
                          out.isEmpty() ? i18n("(no output)") : out, true);
    });
}

/*┌──────────────────────╮
  | Autostart            |
  └──────────────────────╯*/

// Autostart row is [Name, Command, Scope, Enabled, Running, File].
Autostart::Entry autostartEntryFromRow(const QStringList &r)
{
    Autostart::Entry e;
    e.name = r.value(0);
    e.exec = r.value(1);
    e.file = r.value(5);
    e.comment = Autostart::readComment(e.file);
    return e;
}

void wireAutostart(QWidget *parent, CommandTab *tab)
{
    auto addEntry = [parent, tab] {
        AutostartEditDialog dlg(parent);
        if (dlg.exec() != QDialog::Accepted)
            return;
        QString error;
        if (!Autostart::save(dlg.entry(), &error))
            KMessageBox::error(parent, error);
        tab->refresh();
    };
    tab->addBarAction(i18nc("@action", "Add"), QStringLiteral("list-add"), addEntry);
    tab->addMenuAction(i18nc("@action", "Add"), QStringLiteral("list-add"), addEntry);

    tab->addMenuAction(i18nc("@action", "Edit"), QStringLiteral("document-edit"), [parent, tab] {
        const QStringList row = firstSelected(parent, tab);
        if (row.isEmpty())
            return;
        AutostartEditDialog dlg(parent, autostartEntryFromRow(row));
        if (dlg.exec() != QDialog::Accepted)
            return;
        QString error;
        if (!Autostart::save(dlg.entry(), &error))
            KMessageBox::error(parent, error);
        tab->refresh();
    });

    tab->addMenuAction(i18nc("@action", "Toggle"), QStringLiteral("dialog-ok-apply"), [parent, tab] {
        const QStringList row = firstSelected(parent, tab);
        if (row.isEmpty())
            return;
        QString error;
        if (!Autostart::toggle(row.value(5), &error) && !error.isEmpty())
            KMessageBox::error(parent, error);
        tab->refresh();
    });

    tab->addMenuAction(i18nc("@action", "Delete"), QStringLiteral("list-remove"), [parent, tab] {
        const QStringList row = firstSelected(parent, tab);
        if (row.isEmpty())
            return;
        if (!confirm(parent, i18n("Remove autostart entry \"%1\"?", row.value(0)),
                     KStandardGuiItem::del()))
            return;
        QString error;
        if (!Autostart::remove(row.value(5), &error))
            KMessageBox::error(parent, error);
        tab->refresh();
    });
}

/*┌──────────────────────╮
  | Mounts               |
  └──────────────────────╯*/

// Mounts row is [Target, Source, Filesystem, Type, State, Managed, Options].
QVector<QStringList> selectedBindRows(QWidget *parent, CommandTab *tab)
{
    QVector<QStringList> out;
    for (const QStringList &r : tab->selectedRows())
        if (r.value(5) == QLatin1String("bindln"))
            out << r;
    if (out.isEmpty())
        KMessageBox::information(parent, i18n("Please select a bindln-managed mount."));
    return out;
}

void wireMounts(QWidget *parent, CommandTab *tab)
{
    tab->addRowAction({i18nc("@action", "Unmount"), QStringLiteral("media-eject"), [parent](const QStringList &row) {
        if (row.isEmpty())
            return;
        QString error;
        if (!Auth::unmount(row.at(0), &error) && !error.isEmpty())
            KMessageBox::error(parent, error, i18nc("@title:window", "Unmount Failed"));
    }});

    auto newBindLink = [parent, tab] {
        BindLinkDialog dlg(parent);
        dlg.exec();
        tab->refresh();
    };
    tab->addBarAction(i18nc("@action", "New Bind Link"), QStringLiteral("edit-link"), newBindLink);
    tab->addMenuAction(i18nc("@action", "New Bind Link"), QStringLiteral("edit-link"), newBindLink);

    tab->addMenuAction(i18nc("@action", "Unlink Bind"), QStringLiteral("edit-delete"), [parent, tab] {
        const QVector<QStringList> rows = selectedBindRows(parent, tab);
        if (rows.isEmpty())
            return;
        QStringList targets;
        for (const QStringList &r : rows)
            targets << r.value(0);
        if (!confirm(parent, i18n("Unlink these bind links? Real data is not deleted.\n\n%1",
                                  targets.join(QLatin1Char('\n'))),
                     KGuiItem(i18nc("@action:button", "Unlink"))))
            return;
        QStringList errors;
        for (const QString &t : targets) {
            QString out, error;
            if (!Auth::bindln({QStringLiteral("unlink"), t}, &out, &error))
                errors << (error.isEmpty() ? out : error);
        }
        tab->refresh();
        if (!errors.isEmpty())
            KMessageBox::error(parent, errors.join(QLatin1Char('\n')));
    });

    tab->addMenuAction(i18nc("@action", "Bind Status"), QStringLiteral("documentinfo"), [parent, tab] {
        const QVector<QStringList> rows = selectedBindRows(parent, tab);
        if (rows.isEmpty())
            return;
        QStringList blocks;
        for (const QStringList &r : rows)
            blocks << runCmd(QStringLiteral("bindln"), {QStringLiteral("status"), r.value(0)}, 5000);
        Dialogs::showText(parent, i18nc("@title:window", "Bind Link Status"),
                          blocks.join(QStringLiteral("\n---\n")));
    });

    tab->addMenuAction(i18nc("@action", "Open Target"), QStringLiteral("folder-open"), [parent, tab] {
        for (const QStringList &r : selectedBindRows(parent, tab)) {
            const QString source = r.value(1);
            if (!source.isEmpty())
                QProcess::startDetached(QStringLiteral("xdg-open"), {source});
        }
    });
}

/*┌──────────────────────╮
  | Sessions             |
  └──────────────────────╯*/

// Sessions row is [Session, User, …]; id = col 0, user = col 1.
void wireSessions(QWidget *parent, CommandTab *tab)
{
    tab->addMenuAction(i18nc("@action", "End Session"), QStringLiteral("system-log-out"), [parent, tab] {
        const QStringList row = firstSelected(parent, tab);
        if (row.isEmpty())
            return;
        const QString id = row.value(0), user = row.value(1);
        if (!confirm(parent, i18n("End session %1 for user %2? This logs them out and closes their running programs.",
                                  id, user),
                     KGuiItem(i18nc("@action:button", "End Session"))))
            return;
        QProcess p; // own session works directly; others prompt polkit
        p.start(QStringLiteral("loginctl"), {QStringLiteral("terminate-session"), id});
        p.waitForFinished(8000);
        if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
            const QString err = QString::fromUtf8(p.readAllStandardError()).trimmed();
            KMessageBox::error(parent, err.isEmpty() ? i18n("Failed to end the session.") : err);
        }
        tab->refresh();
    });
}

/*┌──────────────────────╮
  | Snaps                |
  └──────────────────────╯*/

// Snaps row is [Snap, Service, Startup, Status, Notes]; service = col 1.
void wireSnaps(QWidget *parent, CommandTab *tab)
{
    auto svcLife = [parent](const QString &text, const QString &icon, const QString &verb) {
        return CommandTab::RowAction{text, icon, [parent, verb](const QStringList &row) {
            const QString service = row.value(1);
            if (service.isEmpty())
                return;
            QString error;
            if (!Auth::snap(verb, service, nullptr, &error) && !error.isEmpty())
                KMessageBox::error(parent, error, i18nc("@title:window", "Snap Action Failed"));
        }};
    };
    tab->addRowAction(svcLife(i18nc("@action", "Start"), QStringLiteral("media-playback-start"), QStringLiteral("start")));
    tab->addRowAction(svcLife(i18nc("@action", "Stop"), QStringLiteral("media-playback-stop"), QStringLiteral("stop")));
    tab->addRowAction(svcLife(i18nc("@action", "Restart"), QStringLiteral("system-reboot"), QStringLiteral("restart")));

    tab->addMenuAction(i18nc("@action", "Logs"), QStringLiteral("text-x-log"), [parent, tab] {
        const QStringList row = firstSelected(parent, tab);
        const QString service = row.value(1);
        if (service.isEmpty())
            return;
        QString out, error;
        Auth::snap(QStringLiteral("logs"), service, &out, &error);
        if (!error.isEmpty()) {
            KMessageBox::error(parent, error);
            return;
        }
        Dialogs::showText(parent, i18nc("@title:window", "Snap Logs: %1", service),
                          out.isEmpty() ? i18n("(no output)") : out, true);
    });
}

} // namespace

MainWindow::MainWindow(QWidget *parent) : KXmlGuiWindow(parent)
{
    mTabs = new QTabWidget(this);
    mTabs->setDocumentMode(true);
    setCentralWidget(mTabs);

    // 1. Services — bespoke D-Bus tab with inline lifecycle editing.
    mTabs->addTab(new ServicesTab(this), i18nc("@title:tab", "Services"));

    // 2. Timers (lifecycle + logs/deps/edit/location via right-click).
    auto *timersTab = new CommandTab(
        {i18nc("@title:column", "Timer"), i18nc("@title:column", "Status"),
         i18nc("@title:column", "Scope"), i18nc("@title:column", "Next Run"),
         i18nc("@title:column", "Last Run"), i18nc("@title:column", "Activates"),
         i18nc("@title:column", "Description")},
        1, Fetchers::timers, {}, this);
    wireTimers(this, timersTab);
    mTabs->addTab(timersTab, i18nc("@title:tab", "Timers"));

    // Tab order matches the original Python TaskPilot: Mounts last (before the
    // new Sessions tab), Cron third.

    // 3. Cron — current user by default; Elevate enables other users.
    auto *cronTab = new CommandTab(
        {i18nc("@title:column", "User"), i18nc("@title:column", "Minute"),
         i18nc("@title:column", "Hour"), i18nc("@title:column", "Day of Month"),
         i18nc("@title:column", "Month"), i18nc("@title:column", "Day of Week"),
         i18nc("@title:column", "Command")},
        -1, [] { return Fetchers::cronFor(Cron::currentUser()); }, {}, this);
    wireCron(this, cronTab);
    mTabs->addTab(cronTab, i18nc("@title:tab", "Cron"));

    // 4. Autostart (XDG .desktop CRUD).
    auto *autostartTab = new CommandTab(
        {i18nc("@title:column", "Name"), i18nc("@title:column", "Command"),
         i18nc("@title:column", "Scope"), i18nc("@title:column", "Enabled"),
         i18nc("@title:column", "Running"), i18nc("@title:column", "File")},
        -1, Fetchers::autostart, {}, this);
    wireAutostart(this, autostartTab);
    mTabs->addTab(autostartTab, i18nc("@title:tab", "Autostart"));

    // 5. Ports — with an Elevate button that switches to the root fetcher so
    //    root-owned sockets resolve (process/path/command no longer blank).
    auto *portsTab = new CommandTab(
        {i18nc("@title:column", "Protocol"), i18nc("@title:column", "Address"),
         i18nc("@title:column", "Port"), i18nc("@title:column", "PID"),
         i18nc("@title:column", "Process"), i18nc("@title:column", "Path"),
         i18nc("@title:column", "Command")},
        -1, Fetchers::ports, {}, this);
    portsTab->addBarAction(i18nc("@action", "Elevate"), QStringLiteral("security-high"), [this, portsTab] {
        // 1. Probe once to trigger the polkit prompt and surface a denial.
        QString error;
        Auth::elevatedPorts(&error);
        if (!error.isEmpty()) {
            KMessageBox::error(this, error, i18nc("@title:window", "Elevation Failed"));
            return;
        }
        // 2. Stay elevated for later refreshes (auth is cached per session).
        portsTab->setFetcher([] { return Auth::elevatedPorts(); });
        portsTab->refresh();
    }, i18nc("@info:tooltip", "Enter your password to reveal the process and command for root-owned sockets."));

    // Terminate/Kill the process holding a port. Tries as the current user, then
    // falls back to the root KAuth helper for processes you don't own.
    auto killHandler = [this, portsTab](const QString &verb, const QString &sig) {
        return [this, portsTab, verb, sig] {
            const QStringList row = firstSelected(this, portsTab);
            if (row.isEmpty())
                return;
            bool ok = false;
            const int pid = row.value(3).toInt(&ok);
            if (!ok || pid <= 1) {
                KMessageBox::information(this, i18n("No process is known for this socket — "
                    "click Elevate first to resolve root-owned sockets."));
                return;
            }
            const QString proc = row.value(4).isEmpty() ? i18n("the process") : row.value(4);
            if (!confirm(this, i18n("%1 %2 (PID %3)?", verb, proc, pid), KGuiItem(verb)))
                return;
            QProcess p;
            p.start(QStringLiteral("kill"), {QStringLiteral("-s"), sig, QString::number(pid)});
            p.waitForFinished(5000);
            const bool done = p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
            if (!done) {
                const QString err = QString::fromUtf8(p.readAllStandardError());
                if (err.contains(QLatin1String("not permitted"), Qt::CaseInsensitive)) {
                    QString error; // root-owned → elevate
                    if (!Auth::killProcess(pid, sig, &error) && !error.isEmpty())
                        KMessageBox::error(this, error);
                } else if (!err.trimmed().isEmpty()) {
                    KMessageBox::error(this, err.trimmed());
                }
            }
            portsTab->refresh();
        };
    };
    portsTab->addMenuAction(i18nc("@action", "Terminate Process (Politely Stop)"), QStringLiteral("process-stop"),
                            killHandler(i18nc("@action", "Terminate"), QStringLiteral("TERM")));
    portsTab->addMenuAction(i18nc("@action", "Kill Process (Force)"), QStringLiteral("edit-delete"),
                            killHandler(i18nc("@action", "Kill"), QStringLiteral("KILL")));

    mTabs->addTab(portsTab, i18nc("@title:tab", "Ports"));

    // 6. Journal.
    mTabs->addTab(new JournalTab(this), i18nc("@title:tab", "Journal"));

    // 7. Snaps.
    auto *snapsTab = new CommandTab(
        {i18nc("@title:column", "Snap"), i18nc("@title:column", "Service"),
         i18nc("@title:column", "Startup"), i18nc("@title:column", "Status"),
         i18nc("@title:column", "Notes")},
        3, Fetchers::snaps, {}, this);
    wireSnaps(this, snapsTab);
    mTabs->addTab(snapsTab, i18nc("@title:tab", "Snaps"));

    // 8. Containers (lifecycle + logs/inspect/shell).
    auto *containersTab = new CommandTab(
        {i18nc("@title:column", "Name"), i18nc("@title:column", "Runtime"),
         i18nc("@title:column", "Image"), i18nc("@title:column", "Status"),
         i18nc("@title:column", "Memory"), i18nc("@title:column", "Ports"),
         i18nc("@title:column", "ID"), i18nc("@title:column", "Created")},
        3, Fetchers::containers, {}, this);
    wireContainers(this, containersTab);
    mTabs->addTab(containersTab, i18nc("@title:tab", "Containers"));

    // 9. Mounts (Unmount + bind-link management) — last, as in the original.
    auto *mountsTab = new CommandTab(
        {i18nc("@title:column", "Target"), i18nc("@title:column", "Source"),
         i18nc("@title:column", "Filesystem"), i18nc("@title:column", "Type"),
         i18nc("@title:column", "State"), i18nc("@title:column", "Managed"),
         i18nc("@title:column", "Options")},
        4, Fetchers::mounts, {}, this);
    wireMounts(this, mountsTab);
    mTabs->addTab(mountsTab, i18nc("@title:tab", "Mounts"));

    // 10. Sessions (logind) — with an End Session action.
    auto *sessionsTab = new CommandTab(
        {i18nc("@title:column", "Session"), i18nc("@title:column", "User"),
         i18nc("@title:column", "UID"), i18nc("@title:column", "Seat"),
         i18nc("@title:column", "TTY"), i18nc("@title:column", "Type"),
         i18nc("@title:column", "State"), i18nc("@title:column", "Remote")},
        6, Fetchers::sessions, {}, this);
    wireSessions(this, sessionsTab);
    mTabs->addTab(sessionsTab, i18nc("@title:tab", "Sessions"));

    setupActions();
    // No main toolbar (each tab has its own below the tabs) and no status bar —
    // both would otherwise add an empty band under the menu.
    setupGUI(Keys | Save | Create, QStringLiteral("taskpilotui.rc"));
    pruneHelpMenu();
}

void MainWindow::setupActions()
{
    KStandardAction::quit(qApp, &QApplication::closeAllWindows, actionCollection());

    // Tools menu: global systemd actions that aren't tied to a selected row.
    auto addTool = [this](const QString &name, const QString &icon, const QString &text,
                          std::function<void()> fn) {
        auto *a = new QAction(QIcon::fromTheme(icon), text, this);
        connect(a, &QAction::triggered, this, [fn = std::move(fn)] { fn(); });
        actionCollection()->addAction(name, a);
    };
    addTool(QStringLiteral("new_service"), QStringLiteral("document-new"), i18nc("@action", "New Service…"),
            [this] { UnitCreatorDialog(this, QStringLiteral("service")).exec(); });
    addTool(QStringLiteral("new_timer"), QStringLiteral("document-new"), i18nc("@action", "New Timer…"),
            [this] { UnitCreatorDialog(this, QStringLiteral("timer")).exec(); });
    addTool(QStringLiteral("failed_units"), QStringLiteral("dialog-error"), i18nc("@action", "Failed Units…"),
            [this] { UnitActions::failedUnits(this); });
    addTool(QStringLiteral("startup_blame"), QStringLiteral("chronometer"), i18nc("@action", "Startup Blame…"),
            [this] { UnitActions::startupBlame(this); });
}

void MainWindow::pruneHelpMenu()
{
    // Hide the standard Handbook (no docs ship) and What's This (no per-widget
    // help) — they were dead menu items / a dead F1. Keep About / Report Bug.
    for (KStandardAction::StandardAction id : {KStandardAction::HelpContents, KStandardAction::WhatsThis}) {
        if (QAction *a = findChild<QAction *>(KStandardAction::name(id))) {
            a->setVisible(false);
            a->setEnabled(false); // also drops the shortcut (e.g. F1)
        }
    }
}
