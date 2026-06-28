// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

/** Client-side wrappers around the org.kde.taskpilot KAuth helper. Each call
    triggers a polkit prompt (cached per session) and runs the work as root.
    Synchronous — invoke off the UI thread (the CommandTab fetch path does). */
namespace Auth {

/** Unmount @p target as root. Returns true on success; on failure sets
    @p error (if non-null) to the helper/polkit message. */
bool unmount(const QString &target, QString *error = nullptr);

/** Listening TCP sockets with full process info, gathered as root so
    root-owned sockets resolve. Returns the Ports-tab rows; on auth denial or
    error returns an empty vector and sets @p error if non-null. */
QVector<QStringList> elevatedPorts(QString *error = nullptr);

/** Run `bindln` as root with @p args (e.g. {"unlink","/path"} or
    {"--mkdir","/src","/target"}). Returns true if bindln exited 0; @p output
    (if non-null) gets bindln's combined stdout+stderr, @p error the failure
    message on a polkit/D-Bus failure. */
bool bindln(const QStringList &args, QString *output = nullptr, QString *error = nullptr);

/** Docker + root-podman containers gathered as root (so they appear without
    docker-group membership). Containers-schema rows; empty on auth denial. */
QVector<QStringList> dockerList(QString *error = nullptr);

/** A user that has a crontab, with its job count (for the Cron user selector). */
struct CronUser {
    QString user;
    int count = 0;
};

/** Users with a crontab (root + login users that have ≥1 job), gathered as root.
    Empty on auth denial/error; sets @p error if non-null. */
QVector<CronUser> cronUsers(QString *error = nullptr);

/** @p user's crontab text fetched as root. An empty string is valid (no
    crontab); distinguish failure via @p error. */
QString cronList(const QString &user, QString *error = nullptr);

/** Replace @p user's crontab with @p text, as root. True on success. */
bool cronSave(const QString &user, const QString &text, QString *error = nullptr);

/** Send @p signal ("TERM" or "KILL") to @p pid as root (for processes the user
    doesn't own). True on success. */
bool killProcess(int pid, const QString &signal, QString *error = nullptr);

/** Write a system unit file (@p path under /etc/systemd/system) as root and
    reload the daemon. True on success. */
bool writeUnit(const QString &path, const QString &content, QString *error = nullptr);

} // namespace Auth
