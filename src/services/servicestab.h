// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>
#include <QWidget>

class ServiceModel;
class ServiceFilterProxy;
class SystemdManager;
class QTableView;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QAction;

/** Services tab: aggregates system + user .service units with lifecycle
    actions (start/stop/restart/enable/disable) over systemd D-Bus, plus
    scope/status/Hide-Defaults filters and a right-click menu. */
class ServicesTab : public QWidget {
    Q_OBJECT
public:
    explicit ServicesTab(QWidget *parent = nullptr);

public Q_SLOTS:
    void refresh();

private:
    enum Action { Start, Stop, Restart, Enable, Disable };
    void runOnSelection(Action action);
    // Run a UnitActions viewer (logs/deps/…) on the first selected unit.
    void viewerOnSelection(void (*fn)(QWidget *, const QString &id, const QString &scope));

    SystemdManager *mSystem;
    SystemdManager *mUser;
    ServiceModel *mModel;
    ServiceFilterProxy *mProxy;
    QTableView *mView;
    QLineEdit *mSearch;
    QComboBox *mScopeFilter;
    QComboBox *mStatusFilter;
    QCheckBox *mHideDefaults;
    QList<QAction *> mActions; // lifecycle actions, mirrored into the context menu
};
