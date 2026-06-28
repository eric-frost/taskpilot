// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <KXmlGuiWindow>

class QTabWidget;

/** TaskPilot main window. Hosts one tab per system-management domain
    (Services, Timers, Mounts, Cron, …). Built on KXmlGui for standard
    KDE menus, toolbars and configurable shortcuts. */
class MainWindow : public KXmlGuiWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    void setupActions();
    void pruneHelpMenu();

    QTabWidget *mTabs;
};
