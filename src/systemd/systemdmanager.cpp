// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#include "systemdmanager.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QHash>

namespace {
const QString s_service = QStringLiteral("org.freedesktop.systemd1");
const QString s_path = QStringLiteral("/org/freedesktop/systemd1");
const QString s_managerIface = QStringLiteral("org.freedesktop.systemd1.Manager");

QDBusConnection busFor(SystemdManager::Scope scope)
{
    return scope == SystemdManager::System ? QDBusConnection::systemBus()
                                           : QDBusConnection::sessionBus();
}

// Call a Manager method with interactive authorization allowed, so systemd lets
// polkit prompt for a password instead of refusing privileged (system-unit) ops.
QDBusMessage callManager(SystemdManager::Scope scope, const QString &method, const QVariantList &args)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(s_service, s_path, s_managerIface, method);
    msg.setArguments(args);
    msg.setInteractiveAuthorizationAllowed(true);
    // BlockWithGui keeps the UI responsive while the synchronous call waits on
    // the polkit password prompt (system-unit changes need authorization).
    return busFor(scope).call(msg, QDBus::BlockWithGui);
}
}

SystemdManager::SystemdManager(Scope scope, QObject *parent)
    : QObject(parent), mScope(scope)
{
    // 1. Probe the manager; user bus is absent for system services and vice-versa.
    QDBusInterface probe(s_service, s_path, s_managerIface, busFor(mScope));
    mAvailable = probe.isValid();
}

QVector<UnitInfo> SystemdManager::listUnits(const QString &suffix)
{
    QVector<UnitInfo> out;
    if (!mAvailable)
        return out;

    const auto bus = busFor(mScope);
    QDBusInterface mgr(s_service, s_path, s_managerIface, bus);

    // 1. ListUnitFiles → every installed unit + its enablement state.
    QHash<QString, UnitInfo> byId;
    {
        QDBusMessage reply = mgr.call(QStringLiteral("ListUnitFiles"));
        if (reply.type() == QDBusMessage::ReplyMessage) {
            const QDBusArgument arg = reply.arguments().value(0).value<QDBusArgument>();
            arg.beginArray();
            while (!arg.atEnd()) {
                arg.beginStructure();
                QString path, state;
                arg >> path >> state;
                arg.endStructure();
                const QString id = path.section(QLatin1Char('/'), -1);
                if (!id.endsWith(suffix))
                    continue;
                UnitInfo u;
                u.id = id;
                u.unitFileState = state;
                u.scope = scopeName();
                u.fragmentPath = path;
                byId.insert(id, u);
            }
            arg.endArray();
        }
    }

    // 2. ListUnits → runtime state for loaded units; merge onto the file list.
    {
        QDBusMessage reply = mgr.call(QStringLiteral("ListUnits"));
        if (reply.type() == QDBusMessage::ReplyMessage) {
            const QDBusArgument arg = reply.arguments().value(0).value<QDBusArgument>();
            arg.beginArray();
            while (!arg.atEnd()) {
                arg.beginStructure();
                QString id, desc, load, active, sub, following;
                QDBusObjectPath obj;
                arg >> id >> desc >> load >> active >> sub >> following >> obj;
                // Trailing: jobId(u), jobType(s), jobPath(o) — skip remainder.
                uint jobId; QString jobType; QDBusObjectPath jobPath;
                arg >> jobId >> jobType >> jobPath;
                arg.endStructure();
                if (!id.endsWith(suffix))
                    continue;
                UnitInfo &u = byId[id]; // creates if absent (loaded-but-no-file unit)
                u.id = id;
                u.description = desc;
                u.loadState = load;
                u.activeState = active;
                u.subState = sub;
                u.objectPath = obj.path();
                if (u.scope.isEmpty())
                    u.scope = scopeName();
            }
            arg.endArray();
        }
    }

    out.reserve(byId.size());
    for (auto it = byId.constBegin(); it != byId.constEnd(); ++it)
        out.append(it.value());
    return out;
}

QVariant SystemdManager::unitProperty(const QString &objectPath, const QString &iface, const QString &prop)
{
    if (objectPath.isEmpty() || !mAvailable)
        return {};
    QDBusInterface props(s_service, objectPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                         busFor(mScope));
    QDBusReply<QVariant> reply = props.call(QStringLiteral("Get"), iface, prop);
    return reply.isValid() ? reply.value() : QVariant();
}

QString SystemdManager::jobMethod(const QString &method, const QString &id, QString &error)
{
    const QDBusMessage reply = callManager(mScope, method, {id, QStringLiteral("replace")});
    if (reply.type() == QDBusMessage::ErrorMessage) {
        error = reply.errorMessage();
        return {};
    }
    return reply.arguments().value(0).value<QDBusObjectPath>().path();
}

bool SystemdManager::startUnit(const QString &id, QString &error)
{
    return !jobMethod(QStringLiteral("StartUnit"), id, error).isEmpty() || error.isEmpty();
}

bool SystemdManager::stopUnit(const QString &id, QString &error)
{
    return !jobMethod(QStringLiteral("StopUnit"), id, error).isEmpty() || error.isEmpty();
}

bool SystemdManager::restartUnit(const QString &id, QString &error)
{
    return !jobMethod(QStringLiteral("RestartUnit"), id, error).isEmpty() || error.isEmpty();
}

bool SystemdManager::enableUnit(const QString &id, QString &error)
{
    const QDBusMessage reply = callManager(mScope, QStringLiteral("EnableUnitFiles"),
                                           {QStringList{id}, false, true});
    if (reply.type() == QDBusMessage::ErrorMessage) {
        error = reply.errorMessage();
        return false;
    }
    callManager(mScope, QStringLiteral("Reload"), {}); // pick up the new symlinks
    return true;
}

bool SystemdManager::disableUnit(const QString &id, QString &error)
{
    const QDBusMessage reply = callManager(mScope, QStringLiteral("DisableUnitFiles"),
                                           {QStringList{id}, false});
    if (reply.type() == QDBusMessage::ErrorMessage) {
        error = reply.errorMessage();
        return false;
    }
    callManager(mScope, QStringLiteral("Reload"), {});
    return true;
}

bool SystemdManager::resetFailed(QString &error)
{
    const QDBusMessage reply = callManager(mScope, QStringLiteral("ResetFailed"), {});
    if (reply.type() == QDBusMessage::ErrorMessage) {
        error = reply.errorMessage();
        return false;
    }
    return true;
}
