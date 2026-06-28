// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

/** Run @p prog with @p args and return its stdout (trimmed of nothing). Empty
    string on failure. Synchronous — callers run it off the fetch path which is
    itself dispatched to a worker thread by CommandTab. */
inline QString runCmd(const QString &prog, const QStringList &args, int timeoutMs = 8000)
{
    QProcess p;
    p.setProcessChannelMode(QProcess::SeparateChannels);
    p.start(prog, args);
    if (!p.waitForStarted(2000))
        return {};
    p.waitForFinished(timeoutMs);
    return QString::fromUtf8(p.readAllStandardOutput());
}

/** True if @p prog is found in PATH. */
inline bool haveCmd(const QString &prog)
{
    return !QStandardPaths::findExecutable(prog).isEmpty();
}
