// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

class QWidget;

/** Per-unit actions shared by the Services and Timers tabs (systemctl/journalctl
    over a unit id + scope, where scope is "System" or "User"). The viewer
    actions open a read-only dialog; lifecycle() runs start/stop/restart. */
namespace UnitActions {

void logs(QWidget *parent, const QString &id, const QString &scope);
void dependencies(QWidget *parent, const QString &id, const QString &scope);
void editFile(QWidget *parent, const QString &id, const QString &scope);
void openLocation(QWidget *parent, const QString &id, const QString &scope);
void envFiles(QWidget *parent, const QString &id, const QString &scope);

/** start/stop/restart @p id via systemctl (used by the Timers tab; Services uses
    D-Bus directly). Returns true on success, else sets @p error. */
bool lifecycle(const QString &verb, const QString &id, const QString &scope, QString *error);

/** systemd-analyze blame viewer (boot-time breakdown). */
void startupBlame(QWidget *parent);

/** Failed-units dashboard (system + user) with a "Reset All Failed" button. */
void failedUnits(QWidget *parent);

} // namespace UnitActions
