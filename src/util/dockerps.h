// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "proc.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

/** Best-effort classification for containers that are actually full VMs, such
    as dockur/qemux images. This deliberately uses only `ps` fields/labels; it
    never inspects mounts, env vars, devices, or other sensitive config. */
inline QString dockerLikeKind(const QString &image, const QString &labels)
{
    const QString haystack = (image + QLatin1Char('\n') + labels).toLower();
    if (haystack.contains(QLatin1String("dockurr/"))
        || haystack.contains(QLatin1String("qemux/"))
        || haystack.contains(QLatin1String("docker-osx"))
        || (haystack.contains(QLatin1String("inside a docker container"))
            && (haystack.contains(QLatin1String("windows"))
                || haystack.contains(QLatin1String("macos"))
                || haystack.contains(QLatin1String("virtual machine")))))
        return QStringLiteral("VM");
    return QStringLiteral("Container");
}

/** Append `docker`/`podman ps -a` rows to @p out in the VM/container schema
    [Name, Kind, Runtime, Host, Image, Status, Memory, Ports, ID, Created].
    Header-only so
    both the app (Fetchers::containers) and the privileged KAuth helper (which
    doesn't link fetchers.cpp) can share it. Only ps/stats are run — never
    `inspect` — so no env vars / mounts / host config are ever exposed.
    @p withStats adds the memory column via `stats --no-stream`.
    @p host labels the Docker context/host, and @p runtimeArgs are inserted before
    the docker subcommand (`--context foo ps …`). */
inline void appendDockerLike(const QString &runtime, QVector<QStringList> &out,
                             bool withStats = true, const QString &host = QString(),
                             const QStringList &runtimeArgs = {})
{
    if (!haveCmd(runtime))
        return;
    QStringList psArgs = runtimeArgs;
    psArgs << QStringLiteral("ps") << QStringLiteral("-a")
           << QStringLiteral("--format") << QStringLiteral("{{json .}}");
    const QString psOut = runCmd(runtime, psArgs);
    if (psOut.isEmpty())
        return;

    // Memory map from `stats --no-stream` (never streaming — would block forever).
    QHash<QString, QString> mem;
    if (withStats) {
        QStringList statsArgs = runtimeArgs;
        statsArgs << QStringLiteral("stats") << QStringLiteral("--no-stream")
                  << QStringLiteral("--format") << QStringLiteral("{{.Name}}\t{{.MemUsage}}");
        const QString statsOut = runCmd(runtime, statsArgs, 6000);
        for (const QString &line : statsOut.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const int t = line.indexOf(QLatin1Char('\t'));
            if (t > 0) mem.insert(line.left(t).trimmed(), line.mid(t + 1).trimmed());
        }
    }

    for (const QString &line : psOut.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QJsonObject c = QJsonDocument::fromJson(line.toUtf8()).object();
        QString name = c.value(QStringLiteral("Names")).toString();
        if (name.isEmpty() && c.value(QStringLiteral("Names")).isArray()) {
            const QJsonArray names = c.value(QStringLiteral("Names")).toArray();
            if (!names.isEmpty())
                name = names.first().toString();
        }
        if (name.isEmpty())
            continue;
        QString ports = c.value(QStringLiteral("Ports")).toString();
        QString status = c.value(QStringLiteral("Status")).toString();
        if (status.isEmpty()) status = c.value(QStringLiteral("State")).toString();
        QString id = c.value(QStringLiteral("ID")).toString();
        if (id.isEmpty()) id = c.value(QStringLiteral("Id")).toString();
        const QString image = c.value(QStringLiteral("Image")).toString();
        const QString labels = c.value(QStringLiteral("Labels")).toString();
        out.append({name, dockerLikeKind(image, labels), runtime, host, image, status,
                    mem.value(name, QStringLiteral("—")), ports, id.left(12),
                    c.value(QStringLiteral("CreatedAt")).toString()});
    }
}

/** Append every Docker context visible to this user. The default local daemon is
    just another context; if permission blocks it, the caller can still merge the
    KAuth-backed root list. */
inline void appendDockerContexts(QVector<QStringList> &out)
{
    if (!haveCmd(QStringLiteral("docker")))
        return;

    const QString contexts = runCmd(QStringLiteral("docker"),
        {QStringLiteral("context"), QStringLiteral("ls"), QStringLiteral("--format"), QStringLiteral("{{json .}}")},
        5000);
    bool usedContext = false;
    for (const QString &line : contexts.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QJsonObject c = QJsonDocument::fromJson(line.toUtf8()).object();
        const QString name = c.value(QStringLiteral("Name")).toString();
        if (name.isEmpty())
            continue;
        usedContext = true;
        appendDockerLike(QStringLiteral("docker"), out, true, name,
                         {QStringLiteral("--context"), name});
    }
    if (!usedContext)
        appendDockerLike(QStringLiteral("docker"), out);
}
