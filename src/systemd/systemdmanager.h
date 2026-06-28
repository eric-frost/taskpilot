// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QVector>

/** A systemd unit as seen over D-Bus, flattened for table display. */
struct UnitInfo {
    QString id;             // e.g. "sshd.service"
    QString description;
    QString loadState;      // loaded / not-found / masked
    QString activeState;    // active / inactive / failed / activating
    QString subState;       // running / dead / exited …
    QString unitFileState;  // enabled / disabled / static / masked …
    QString scope;          // "System" or "User"
    QString objectPath;     // D-Bus path, empty if not loaded
    QString fragmentPath;   // unit-file path; /usr/lib/systemd & /lib/systemd = a distro default
};

/** Thin wrapper over the org.freedesktop.systemd1 Manager on one bus
    (system or session). Aggregates ListUnits + ListUnitFiles so disabled
    and inactive units appear too, mirroring `systemctl list-unit-files`. */
class SystemdManager : public QObject {
    Q_OBJECT
public:
    enum Scope { System, User };
    explicit SystemdManager(Scope scope, QObject *parent = nullptr);

    bool isAvailable() const { return mAvailable; }
    QString scopeName() const { return mScope == System ? QStringLiteral("System") : QStringLiteral("User"); }

    /** Fetch all units whose id ends in @p suffix (".service", ".timer", ".mount"). */
    QVector<UnitInfo> listUnits(const QString &suffix);

    /** Read one property of a loaded unit object via org.freedesktop.DBus.Properties.
        @p iface e.g. "org.freedesktop.systemd1.Timer". Returns an invalid QVariant
        if @p objectPath is empty or the call fails. */
    QVariant unitProperty(const QString &objectPath, const QString &iface, const QString &prop);

    // Unit lifecycle. Return true on success; on failure @p error is set.
    bool startUnit(const QString &id, QString &error);
    bool stopUnit(const QString &id, QString &error);
    bool restartUnit(const QString &id, QString &error);
    bool enableUnit(const QString &id, QString &error);
    bool disableUnit(const QString &id, QString &error);

    /** Clear the failed state of all units on this bus. */
    bool resetFailed(QString &error);

private:
    QString jobMethod(const QString &method, const QString &id, QString &error);

    Scope mScope;
    bool mAvailable = false;
};
