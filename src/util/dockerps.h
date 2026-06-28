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

/** Append `docker`/`podman ps -a` rows to @p out in the Containers schema
    [Name, Runtime, Image, Status, Memory, Ports, ID, Created]. Header-only so
    both the app (Fetchers::containers) and the privileged KAuth helper (which
    doesn't link fetchers.cpp) can share it. Only ps/stats are run — never
    `inspect` — so no env vars / mounts / host config are ever exposed.
    @p withStats adds the memory column via `stats --no-stream`; the KAuth helper
    skips it (it's slow enough to blow the D-Bus reply timeout when run as root). */
inline void appendDockerLike(const QString &runtime, QVector<QStringList> &out, bool withStats = true)
{
    if (!haveCmd(runtime))
        return;
    const QString psOut = runCmd(runtime, {QStringLiteral("ps"), QStringLiteral("-a"),
        QStringLiteral("--format"), QStringLiteral("{{json .}}")});
    if (psOut.isEmpty())
        return;

    // Memory map from `stats --no-stream` (never streaming — would block forever).
    QHash<QString, QString> mem;
    if (withStats) {
        const QString statsOut = runCmd(runtime, {QStringLiteral("stats"), QStringLiteral("--no-stream"),
            QStringLiteral("--format"), QStringLiteral("{{.Name}}\t{{.MemUsage}}")}, 6000);
        for (const QString &line : statsOut.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const int t = line.indexOf(QLatin1Char('\t'));
            if (t > 0) mem.insert(line.left(t).trimmed(), line.mid(t + 1).trimmed());
        }
    }

    for (const QString &line : psOut.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QJsonObject c = QJsonDocument::fromJson(line.toUtf8()).object();
        QString name = c.value(QStringLiteral("Names")).toString();
        if (name.isEmpty() && c.value(QStringLiteral("Names")).isArray())
            name = c.value(QStringLiteral("Names")).toArray().first().toString();
        if (name.isEmpty())
            continue;
        QString ports = c.value(QStringLiteral("Ports")).toString();
        QString status = c.value(QStringLiteral("Status")).toString();
        if (status.isEmpty()) status = c.value(QStringLiteral("State")).toString();
        QString id = c.value(QStringLiteral("ID")).toString();
        if (id.isEmpty()) id = c.value(QStringLiteral("Id")).toString();
        out.append({name, runtime, c.value(QStringLiteral("Image")).toString(), status,
                    mem.value(name, QStringLiteral("—")), ports, id.left(12),
                    c.value(QStringLiteral("CreatedAt")).toString()});
    }
}
