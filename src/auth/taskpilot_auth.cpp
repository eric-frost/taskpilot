// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#include "taskpilot_auth.h"

#include "util/dockerps.h"

#include <KAuth/HelperSupport>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QVariant>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <pwd.h>

namespace {

// Reject anything that isn't a plain absolute path — this runs as root.
bool validTarget(const QString &target)
{
    return !target.isEmpty() && target.startsWith(QLatin1Char('/'))
        && !target.contains(QLatin1Char('\n')) && !target.contains(QChar(QChar::Null));
}

ActionReply errorReply(const QString &message)
{
    ActionReply reply = ActionReply::HelperErrorReply();
    reply.setErrorDescription(message);
    return reply;
}

// Constrain $PATH for privileged child processes so a world-writable PATH entry
// can't trojan `docker`/`crontab` into running as root.
void hardenPath()
{
    qputenv("PATH", "/usr/bin:/bin:/usr/sbin:/sbin");
}

// Mirror of the Python valid_user: strict name shape AND must resolve in passwd.
// Anchored with \A…\z (not ^…$) so an embedded newline can never slip through.
bool validUser(const QString &user)
{
    static const QRegularExpression re(QStringLiteral("\\A[A-Za-z_][A-Za-z0-9_-]*[$]?\\z"));
    if (user.isEmpty() || user.contains(QLatin1Char('\n')) || user.contains(QChar(QChar::Null)))
        return false;
    if (!re.match(user).hasMatch())
        return false;
    return getpwnam(user.toLocal8Bit().constData()) != nullptr;
}

// True only if @p map[key] is a genuine string (not a coerced int/list/map).
bool isString(const QVariantMap &map, const QString &key)
{
    return map.value(key).typeId() == QMetaType::QString;
}

// Count real job lines: classic (>=6 fields) or @-shortcuts.
int countCronJobs(const QString &text)
{
    int n = 0;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QString t = line.trimmed();
        if (t.isEmpty() || t.startsWith(QLatin1Char('#')))
            continue;
        if (t.startsWith(QLatin1Char('@'))
            || t.split(QRegularExpression(QStringLiteral("\\s+"))).size() >= 6)
            ++n;
    }
    return n;
}

// `crontab -u USER -l` text, or empty on no-crontab/error. Caller pre-validates user.
QString readCrontab(const QString &user)
{
    QProcess p;
    p.start(QStringLiteral("crontab"), {QStringLiteral("-u"), user, QStringLiteral("-l")});
    if (!p.waitForStarted(2000))
        return {};
    p.waitForFinished(8000);
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        return {};
    return QString::fromUtf8(p.readAllStandardOutput());
}

// Users with a non-empty crontab. Spool dirs are the authoritative (root-only)
// source; passwd adds login users. Names are never used to build paths and never
// read directly — only fed to `crontab -u`, which enforces real ownership.
QVariantList cronUsersWithCounts()
{
    QSet<QString> candidates;
    for (const QString &dir : {QStringLiteral("/var/spool/cron/crontabs"),
                               QStringLiteral("/var/spool/cron/tabs"),
                               QStringLiteral("/var/spool/cron")}) {
        QDir d(dir);
        if (!d.exists())
            continue;
        for (const QFileInfo &fi : d.entryInfoList(QDir::Files | QDir::Hidden)) {
            if (fi.isSymLink()) // never follow a symlink planted in the spool
                continue;
            const QString name = fi.fileName();
            if (!name.startsWith(QLatin1Char('.')) && validUser(name))
                candidates.insert(name);
        }
    }
    setpwent();
    while (struct passwd *pw = getpwent()) {
        const QString name = QString::fromLocal8Bit(pw->pw_name);
        const QString shell = QString::fromLocal8Bit(pw->pw_shell);
        if (name == QLatin1String("root") || pw->pw_uid >= 1000
            || !(shell.endsWith(QLatin1String("/nologin")) || shell.endsWith(QLatin1String("/false"))))
            candidates.insert(name);
    }
    endpwent();

    QVariantList out;
    for (const QString &u : candidates) {
        if (!validUser(u))
            continue;
        const int count = countCronJobs(readCrontab(u));
        if (count > 0)
            out.append(QVariantMap{{QStringLiteral("user"), u}, {QStringLiteral("count"), count}});
    }
    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
        const QString ua = a.toMap().value(QStringLiteral("user")).toString();
        const QString ub = b.toMap().value(QStringLiteral("user")).toString();
        if ((ua == QLatin1String("root")) != (ub == QLatin1String("root")))
            return ua == QLatin1String("root");
        return ua < ub;
    });
    return out;
}

} // namespace

ActionReply TaskpilotAuthHelper::unmount(const QVariantMap &args)
{
    const QString target = args.value(QStringLiteral("target")).toString();
    if (!validTarget(target))
        return errorReply(QStringLiteral("invalid target"));

    QProcess p;
    p.start(QStringLiteral("umount"), {target});
    if (!p.waitForStarted(2000))
        return errorReply(QStringLiteral("could not run umount"));
    p.waitForFinished(15000);
    if (p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0)
        return ActionReply::SuccessReply();

    const QString err = QString::fromUtf8(p.readAllStandardError()).trimmed();
    return errorReply(err.isEmpty() ? QStringLiteral("umount failed") : err);
}

ActionReply TaskpilotAuthHelper::ports(const QVariantMap &)
{
    static const QRegularExpression userRe(
        QStringLiteral("users:\\(\\(\"([^\"]*)\",pid=(\\d+)"));
    static const QRegularExpression wsRe(QStringLiteral("\\s+"));

    QProcess p;
    p.start(QStringLiteral("ss"), {QStringLiteral("-tlnpH")});
    if (!p.waitForStarted(2000))
        return errorReply(QStringLiteral("could not run ss"));
    p.waitForFinished(8000);
    const QString ss = QString::fromUtf8(p.readAllStandardOutput());

    QVariantList rows;
    for (const QString &line : ss.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList parts = line.split(wsRe, Qt::SkipEmptyParts);
        if (parts.size() < 4)
            continue;
        const QString local = parts.at(3);
        const int colon = local.lastIndexOf(QLatin1Char(':'));
        const QString addr = colon >= 0 ? local.left(colon) : local;
        const QString port = colon >= 0 ? local.mid(colon + 1) : QStringLiteral("?");

        QString proc, pid, path, cmd;
        const auto m = userRe.match(line);
        if (m.hasMatch()) {
            proc = m.captured(1);
            pid = m.captured(2);
            path = QFile::symLinkTarget(QStringLiteral("/proc/%1/exe").arg(pid));
            QFile c(QStringLiteral("/proc/%1/cmdline").arg(pid));
            if (c.open(QIODevice::ReadOnly))
                cmd = QString::fromUtf8(c.readAll().replace('\0', ' ')).trimmed();
        }
        rows.append(QVariant(QStringList{QStringLiteral("tcp"), addr, port, pid, proc, path, cmd}));
    }

    ActionReply reply = ActionReply::SuccessReply();
    QVariantMap data;
    data.insert(QStringLiteral("rows"), rows);
    reply.setData(data);
    return reply;
}

ActionReply TaskpilotAuthHelper::bindln(const QVariantMap &args)
{
    static const QSet<QString> flags = {
        QStringLiteral("--mkdir"), QStringLiteral("--move-existing"), QStringLiteral("--force"),
        QStringLiteral("--late"), QStringLiteral("--keep-mountpoint"), QStringLiteral("--dry-run")};
    static const QSet<QString> commands = {QStringLiteral("unlink"), QStringLiteral("fix")};

    const QStringList list = args.value(QStringLiteral("args")).toStringList();
    if (list.isEmpty() || list.size() > 20)
        return errorReply(QStringLiteral("invalid bindln args"));
    for (const QString &a : list) {
        if (a.contains(QLatin1Char('\n')) || a.contains(QChar(QChar::Null)))
            return errorReply(QStringLiteral("invalid bindln arg"));
        if (a.startsWith(QLatin1String("--"))) {
            if (!flags.contains(a))
                return errorReply(QStringLiteral("unsupported bindln flag: ") + a);
        } else if (!commands.contains(a) && !a.startsWith(QLatin1Char('/'))) {
            return errorReply(QStringLiteral("bindln path must be absolute: ") + a);
        }
    }

    QProcess p;
    p.start(QStringLiteral("bindln"), list);
    if (!p.waitForStarted(2000))
        return errorReply(QStringLiteral("could not run bindln"));
    p.waitForFinished(15000);

    ActionReply reply = ActionReply::SuccessReply();
    QVariantMap data;
    data.insert(QStringLiteral("stdout"), QString::fromUtf8(p.readAllStandardOutput()));
    data.insert(QStringLiteral("stderr"), QString::fromUtf8(p.readAllStandardError()));
    data.insert(QStringLiteral("code"), p.exitCode());
    reply.setData(data);
    return reply; // success of the D-Bus call; caller checks "code" for bindln's result
}

ActionReply TaskpilotAuthHelper::dockerlist(const QVariantMap &)
{
    hardenPath();
    // Hardcoded runtimes — never trust a client-supplied program name as root.
    // Skip `stats` (withStats=false): it's slow enough as root to time out the call.
    QVector<QStringList> rowsVec;
    appendDockerLike(QStringLiteral("docker"), rowsVec, false);
    appendDockerLike(QStringLiteral("podman"), rowsVec, false);

    QVariantList rows;
    rows.reserve(rowsVec.size());
    for (const QStringList &r : rowsVec)
        rows.append(QVariant(r));

    ActionReply reply = ActionReply::SuccessReply();
    reply.setData({{QStringLiteral("rows"), rows}});
    return reply;
}

ActionReply TaskpilotAuthHelper::cronread(const QVariantMap &args)
{
    hardenPath();
    const QString cmd = args.value(QStringLiteral("cmd")).toString();

    if (cmd == QLatin1String("users")) {
        ActionReply reply = ActionReply::SuccessReply();
        reply.setData({{QStringLiteral("users"), cronUsersWithCounts()}});
        return reply;
    }
    if (cmd == QLatin1String("list")) {
        if (!isString(args, QStringLiteral("user")))
            return errorReply(QStringLiteral("invalid user"));
        const QString user = args.value(QStringLiteral("user")).toString();
        if (!validUser(user))
            return errorReply(QStringLiteral("invalid user"));
        ActionReply reply = ActionReply::SuccessReply();
        reply.setData({{QStringLiteral("text"), readCrontab(user)}});
        return reply;
    }
    return errorReply(QStringLiteral("unknown cron command"));
}

ActionReply TaskpilotAuthHelper::cronsave(const QVariantMap &args)
{
    hardenPath();
    if (!isString(args, QStringLiteral("user")) || !isString(args, QStringLiteral("text")))
        return errorReply(QStringLiteral("invalid crontab save request"));
    const QString user = args.value(QStringLiteral("user")).toString();
    QString text = args.value(QStringLiteral("text")).toString();
    if (!validUser(user))
        return errorReply(QStringLiteral("invalid user"));
    if (text.contains(QChar(QChar::Null)))
        return errorReply(QStringLiteral("invalid crontab text"));
    if (text.size() > 64 * 1024) // a human crontab is tiny; cap the DoS surface
        return errorReply(QStringLiteral("crontab too large"));
    if (!text.isEmpty() && !text.endsWith(QLatin1Char('\n')))
        text.append(QLatin1Char('\n')); // avoid crontab dropping the last line

    QProcess p;
    p.start(QStringLiteral("crontab"), {QStringLiteral("-u"), user, QStringLiteral("-")});
    if (!p.waitForStarted(2000))
        return errorReply(QStringLiteral("could not run crontab"));
    p.write(text.toUtf8());
    p.closeWriteChannel();
    p.waitForFinished(15000);
    if (p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0)
        return ActionReply::SuccessReply();
    const QString err = QString::fromUtf8(p.readAllStandardError()).trimmed();
    return errorReply(err.isEmpty() ? QStringLiteral("crontab save failed") : err);
}

ActionReply TaskpilotAuthHelper::kill(const QVariantMap &args)
{
    bool ok = false;
    const int pid = args.value(QStringLiteral("pid")).toInt(&ok);
    if (!ok || pid <= 1) // never signal init or an invalid pid
        return errorReply(QStringLiteral("invalid pid"));
    const QString sig = args.value(QStringLiteral("signal")).toString();
    int signum;
    if (sig == QLatin1String("TERM"))
        signum = SIGTERM;
    else if (sig == QLatin1String("KILL"))
        signum = SIGKILL;
    else
        return errorReply(QStringLiteral("invalid signal"));

    if (::kill(static_cast<pid_t>(pid), signum) == 0)
        return ActionReply::SuccessReply();
    return errorReply(QString::fromLocal8Bit(strerror(errno)));
}

ActionReply TaskpilotAuthHelper::writeunit(const QVariantMap &args)
{
    hardenPath();
    if (!isString(args, QStringLiteral("path")) || !isString(args, QStringLiteral("content")))
        return errorReply(QStringLiteral("invalid request"));
    const QString path = args.value(QStringLiteral("path")).toString();
    const QString content = args.value(QStringLiteral("content")).toString();

    // Restrict to a real unit file directly under /etc/systemd/system (no traversal).
    static const QString dir = QStringLiteral("/etc/systemd/system/");
    static const QRegularExpression nameRe(QStringLiteral("\\A[A-Za-z0-9._@-]+\\.(service|timer)\\z"));
    if (!path.startsWith(dir))
        return errorReply(QStringLiteral("path must be under /etc/systemd/system"));
    const QString name = path.mid(dir.size());
    if (name.contains(QLatin1Char('/')) || !nameRe.match(name).hasMatch())
        return errorReply(QStringLiteral("invalid unit file name"));
    if (content.contains(QChar(QChar::Null)) || content.size() > 64 * 1024)
        return errorReply(QStringLiteral("invalid content"));

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return errorReply(f.errorString());
    f.write(content.toUtf8());
    f.close();
    f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);

    QProcess p; // pick up the new unit
    p.start(QStringLiteral("systemctl"), {QStringLiteral("daemon-reload")});
    if (p.waitForStarted(2000))
        p.waitForFinished(8000);
    return ActionReply::SuccessReply();
}

ActionReply TaskpilotAuthHelper::snap(const QVariantMap &args)
{
    hardenPath();
    static const QSet<QString> verbs = {QStringLiteral("start"), QStringLiteral("stop"),
                                        QStringLiteral("restart"), QStringLiteral("logs")};
    static const QRegularExpression svcRe(QStringLiteral("\\A[A-Za-z0-9][A-Za-z0-9._-]*\\z"));
    const QString verb = args.value(QStringLiteral("verb")).toString();
    const QString service = args.value(QStringLiteral("service")).toString();
    if (!verbs.contains(verb))
        return errorReply(QStringLiteral("invalid verb"));
    if (!isString(args, QStringLiteral("service")) || !svcRe.match(service).hasMatch())
        return errorReply(QStringLiteral("invalid service"));

    QProcess p;
    const QStringList a = verb == QLatin1String("logs")
        ? QStringList{QStringLiteral("logs"), QStringLiteral("-n"), QStringLiteral("200"), service}
        : QStringList{verb, service};
    p.start(QStringLiteral("snap"), a);
    if (!p.waitForStarted(2000))
        return errorReply(QStringLiteral("could not run snap"));
    p.waitForFinished(15000);

    if (verb == QLatin1String("logs")) {
        const QString out = QString::fromUtf8(p.readAllStandardOutput())
                          + QString::fromUtf8(p.readAllStandardError());
        ActionReply reply = ActionReply::SuccessReply();
        reply.setData({{QStringLiteral("out"), out}});
        return reply;
    }
    if (p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0)
        return ActionReply::SuccessReply();
    const QString err = QString::fromUtf8(p.readAllStandardError()).trimmed();
    return errorReply(err.isEmpty() ? QStringLiteral("snap command failed") : err);
}

KAUTH_HELPER_MAIN("io.github.ericfrost.taskpilot", TaskpilotAuthHelper)
