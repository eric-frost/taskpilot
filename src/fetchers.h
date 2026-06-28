// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QStringList>
#include <QVector>

/** Data providers for the CommandTab-based tabs. Each returns table rows
    (one QStringList per row) matching the headers wired up in MainWindow.
    They shell out to the same tools the original TaskPilot used. */
namespace Fetchers {
QVector<QStringList> timers();      // systemd .timer units (D-Bus)
QVector<QStringList> mounts();      // findmnt + bindln
QVector<QStringList> ports();       // ss -tlnp
QVector<QStringList> autostart();   // XDG autostart .desktop entries
QVector<QStringList> snaps();       // snap services
QVector<QStringList> containers();  // docker / podman / lxd
QVector<QStringList> sessions();    // logind sessions (loginctl)

/** One user's crontab as rows [User,Min,Hour,DoM,Month,DoW,Command]. The current
    user is read directly; other users via KAuth. The Cron::kAllUsers sentinel
    aggregates every crontab-bearing user (requires prior elevation). */
QVector<QStringList> cronFor(const QString &user);
}
