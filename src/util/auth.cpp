// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#include "auth.h"

#include <KAuth/Action>
#include <KAuth/ExecuteJob>

#include <QCoreApplication>
#include <QThread>

using namespace KAuth;

namespace {

struct Result {
    bool ok = false;
    QVariantMap data;
    QString error;
};

// Run a helper action and collect its result. KAuth/QDBus is thread-affine to
// the thread that owns the bus connection, and CommandTab calls fetchers on a
// worker thread — so always marshal the call onto the main thread (the worker
// blocks until it returns). Without this the reply is delivered to a thread
// whose event loop never runs it, and the fetch hangs until the timeout.
Result runOnMain(const QString &name, const QVariantMap &args)
{
    auto body = [&]() -> Result {
        Action action(name);
        action.setHelperId(QStringLiteral("io.github.ericfrost.taskpilot"));
        action.setArguments(args);
        // Some actions shell out to several slow commands as root; raise the
        // D-Bus reply timeout above the 25 s default.
        action.setTimeout(90000);
        ExecuteJob *job = action.execute();
        if (job->exec())
            return {true, job->data(), {}};
        return {false, {}, job->errorText().isEmpty() ? job->errorString() : job->errorText()};
    };

    QCoreApplication *app = QCoreApplication::instance();
    if (!app || QThread::currentThread() == app->thread())
        return body();

    Result result;
    QMetaObject::invokeMethod(app, [&] { result = body(); }, Qt::BlockingQueuedConnection);
    return result;
}

// Map a list-of-QStringList data key into rows.
QVector<QStringList> toRows(const QVariantMap &data, const QString &key)
{
    QVector<QStringList> rows;
    const QVariantList list = data.value(key).toList();
    rows.reserve(list.size());
    for (const QVariant &v : list)
        rows.append(v.toStringList());
    return rows;
}

} // namespace

bool Auth::unmount(const QString &target, QString *error)
{
    const Result r = runOnMain(QStringLiteral("io.github.ericfrost.taskpilot.unmount"),
                               {{QStringLiteral("target"), target}});
    if (!r.ok && error)
        *error = r.error;
    return r.ok;
}

QVector<QStringList> Auth::elevatedPorts(QString *error)
{
    const Result r = runOnMain(QStringLiteral("io.github.ericfrost.taskpilot.ports"), {});
    if (!r.ok && error)
        *error = r.error;
    return toRows(r.data, QStringLiteral("rows"));
}

bool Auth::bindln(const QStringList &args, QString *output, QString *error)
{
    const Result r = runOnMain(QStringLiteral("io.github.ericfrost.taskpilot.bindln"),
                               {{QStringLiteral("args"), args}});
    if (!r.ok) {
        if (error)
            *error = r.error;
        return false;
    }
    if (output)
        *output = r.data.value(QStringLiteral("stdout")).toString()
                + r.data.value(QStringLiteral("stderr")).toString();
    return r.data.value(QStringLiteral("code")).toInt() == 0;
}

QVector<QStringList> Auth::dockerList(QString *error)
{
    const Result r = runOnMain(QStringLiteral("io.github.ericfrost.taskpilot.dockerlist"), {});
    if (!r.ok && error)
        *error = r.error;
    return toRows(r.data, QStringLiteral("rows"));
}

QVector<Auth::CronUser> Auth::cronUsers(QString *error)
{
    const Result r = runOnMain(QStringLiteral("io.github.ericfrost.taskpilot.cronread"),
                               {{QStringLiteral("cmd"), QStringLiteral("users")}});
    if (!r.ok && error)
        *error = r.error;
    QVector<CronUser> out;
    const QVariantList list = r.data.value(QStringLiteral("users")).toList();
    out.reserve(list.size());
    for (const QVariant &v : list) {
        const QVariantMap m = v.toMap();
        out.append({m.value(QStringLiteral("user")).toString(),
                    m.value(QStringLiteral("count")).toInt()});
    }
    return out;
}

QString Auth::cronList(const QString &user, QString *error)
{
    const Result r = runOnMain(QStringLiteral("io.github.ericfrost.taskpilot.cronread"),
                               {{QStringLiteral("cmd"), QStringLiteral("list")},
                                {QStringLiteral("user"), user}});
    if (!r.ok && error)
        *error = r.error;
    return r.data.value(QStringLiteral("text")).toString();
}

bool Auth::cronSave(const QString &user, const QString &text, QString *error)
{
    const Result r = runOnMain(QStringLiteral("io.github.ericfrost.taskpilot.cronsave"),
                               {{QStringLiteral("user"), user}, {QStringLiteral("text"), text}});
    if (!r.ok && error)
        *error = r.error;
    return r.ok;
}

bool Auth::killProcess(int pid, const QString &signal, QString *error)
{
    const Result r = runOnMain(QStringLiteral("io.github.ericfrost.taskpilot.kill"),
                               {{QStringLiteral("pid"), pid}, {QStringLiteral("signal"), signal}});
    if (!r.ok && error)
        *error = r.error;
    return r.ok;
}

bool Auth::writeUnit(const QString &path, const QString &content, QString *error)
{
    const Result r = runOnMain(QStringLiteral("io.github.ericfrost.taskpilot.writeunit"),
                               {{QStringLiteral("path"), path}, {QStringLiteral("content"), content}});
    if (!r.ok && error)
        *error = r.error;
    return r.ok;
}

bool Auth::snap(const QString &verb, const QString &service, QString *output, QString *error)
{
    const Result r = runOnMain(QStringLiteral("io.github.ericfrost.taskpilot.snap"),
                               {{QStringLiteral("verb"), verb}, {QStringLiteral("service"), service}});
    if (output)
        *output = r.data.value(QStringLiteral("out")).toString();
    if (!r.ok && error)
        *error = r.error;
    return r.ok;
}
