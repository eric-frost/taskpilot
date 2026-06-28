// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <KAuth/ActionReply>

#include <QObject>
#include <QVariantMap>

using namespace KAuth;

/** Privileged helper run as root via polkit/D-Bus (KAuth). Each public slot is
    a polkit action declared in org.kde.taskpilot.actions; arguments arrive in a
    QVariantMap and every one is validated before use. Replaces the old
    pkexec/taskpilot-elevate.py path. */
class TaskpilotAuthHelper : public QObject {
    Q_OBJECT
public Q_SLOTS:
    /** unmount a single target path (org.kde.taskpilot.unmount). */
    ActionReply unmount(const QVariantMap &args);

    /** Listening TCP sockets with full process info, resolved as root so
        root-owned sockets aren't blank (org.kde.taskpilot.ports). Returns
        rows under data key "rows" as a list of QStringList. */
    ActionReply ports(const QVariantMap &args);

    /** Run `bindln` as root with whitelisted args (org.kde.taskpilot.bindln);
        args arrive as a QStringList under key "args". Returns stdout/stderr/code
        in the reply data. */
    ActionReply bindln(const QVariantMap &args);

    /** List docker + root podman containers as root so they appear without
        docker-group membership (org.kde.taskpilot.dockerlist). No client input;
        only ps/stats are run (never inspect). Rows under data key "rows". */
    ActionReply dockerlist(const QVariantMap &args);

    /** Read another user's crontab as root (org.kde.taskpilot.cronread, session).
        args "cmd"="users" → data["users"]=[{user,count}]; "cmd"="list"+"user"
        → data["text"]. Read-only. */
    ActionReply cronread(const QVariantMap &args);

    /** Replace a user's crontab as root (org.kde.taskpilot.cronsave; NOT session-
        cached — each write re-authenticates). args "user"+"text". */
    ActionReply cronsave(const QVariantMap &args);

    /** Send a signal to a process as root (org.kde.taskpilot.kill). Args:
        "pid" (int > 1) and "signal" ("TERM" | "KILL"). */
    ActionReply kill(const QVariantMap &args);

    /** Write a systemd unit file under /etc/systemd/system as root, then reload
        the daemon (org.kde.taskpilot.writeunit). Args "path" + "content". */
    ActionReply writeunit(const QVariantMap &args);
};
