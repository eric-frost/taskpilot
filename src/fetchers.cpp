// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fetchers.h"

#include "editors/cronedit.h"
#include "systemd/systemdmanager.h"
#include "util/auth.h"
#include "util/dockerps.h"
#include "util/proc.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

namespace {

/** Format a systemd usec-since-epoch timestamp (0 = never) as a local datetime. */
QString fmtUsec(qulonglong usec)
{
    if (usec == 0 || usec == quint64(-1))
        return QStringLiteral("—");
    return QDateTime::fromMSecsSinceEpoch(qint64(usec / 1000))
        .toString(QStringLiteral("yyyy-MM-dd hh:mm"));
}

void appendTimers(SystemdManager &mgr, QVector<QStringList> &out)
{
    if (!mgr.isAvailable())
        return;
    const QString iface = QStringLiteral("org.freedesktop.systemd1.Timer");
    for (const UnitInfo &u : mgr.listUnits(QStringLiteral(".timer"))) {
        QString next = QStringLiteral("—"), last = QStringLiteral("—"), activates;
        if (!u.objectPath.isEmpty()) {
            next = fmtUsec(mgr.unitProperty(u.objectPath, iface,
                QStringLiteral("NextElapseUSecRealtime")).toULongLong());
            last = fmtUsec(mgr.unitProperty(u.objectPath, iface,
                QStringLiteral("LastTriggerUSec")).toULongLong());
            activates = mgr.unitProperty(u.objectPath, iface, QStringLiteral("Unit")).toString();
        }
        const QString status = u.activeState.isEmpty()
            ? QString() : QStringLiteral("%1 (%2)").arg(u.activeState, u.subState);
        out.append({u.id, status, u.scope, next, last, activates, u.description});
    }
}

void flattenMounts(const QJsonArray &nodes, const QHash<QString, QJsonObject> &bindln,
                   QSet<QString> &seen, QVector<QStringList> &out)
{
    static const QSet<QString> pseudo = {
        QStringLiteral("proc"), QStringLiteral("sysfs"), QStringLiteral("devpts"),
        QStringLiteral("securityfs"), QStringLiteral("pstore"), QStringLiteral("cgroup2"),
        QStringLiteral("debugfs"), QStringLiteral("tracefs"), QStringLiteral("hugetlbfs"),
        QStringLiteral("mqueue"), QStringLiteral("configfs"), QStringLiteral("binfmt_misc"),
        QStringLiteral("fusectl"), QStringLiteral("ramfs"), QStringLiteral("autofs"),
        QStringLiteral("bpf"), QStringLiteral("devtmpfs"), QStringLiteral("efivarfs"),
        QStringLiteral("nsfs")};

    for (const QJsonValue &v : nodes) {
        const QJsonObject n = v.toObject();
        const QString target = n.value(QStringLiteral("target")).toString();
        const QString fstype = n.value(QStringLiteral("fstype")).toString();
        QString source = n.value(QStringLiteral("source")).toString();
        const QString options = n.value(QStringLiteral("options")).toString();

        QString mtype = QStringLiteral("disk");
        if (fstype.contains(QLatin1String("fuse"))) mtype = QStringLiteral("fuse");
        else if (fstype == QLatin1String("tmpfs")) mtype = QStringLiteral("tmpfs");
        else if (fstype == QLatin1String("nfs") || fstype == QLatin1String("cifs")) mtype = QStringLiteral("network");
        if (pseudo.contains(fstype) || target.startsWith(QLatin1String("/run/"))
            || target.startsWith(QLatin1String("/dev/")))
            mtype = QStringLiteral("virtual");
        if (mtype == QLatin1String("fuse")
            && (options.contains(QLatin1String("bindfs")) || target.startsWith(QLatin1String("/home/projects/"))))
            mtype = QStringLiteral("bindfs");

        QString state = QStringLiteral("mounted"), managed;
        if (bindln.contains(target)) {
            const QJsonObject m = bindln.value(target);
            seen.insert(target);
            source = m.value(QStringLiteral("source")).toString(source);
            mtype = QStringLiteral("bindln");
            managed = QStringLiteral("bindln");
            state = QStringLiteral("mounted (%1, %2)")
                .arg(m.value(QStringLiteral("active")).toString(),
                     m.value(QStringLiteral("enabled")).toString());
        } else if (options.split(QLatin1Char(',')).contains(QLatin1String("bind"))) {
            mtype = QStringLiteral("bind");
        }

        out.append({target, source, fstype, mtype, state, managed, options});
        if (n.contains(QStringLiteral("children")))
            flattenMounts(n.value(QStringLiteral("children")).toArray(), bindln, seen, out);
    }
}

// appendDockerLike now lives in util/dockerps.h (shared with the KAuth helper).

// LXD (`lxc`) and its fork Incus (`incus`) share the same JSON list format.
void appendIncusLxd(QVector<QStringList> &out)
{
    const QVector<QPair<QString, QString>> clis = {
        {QStringLiteral("incus"), QStringLiteral("incus")},
        {QStringLiteral("lxc"), QStringLiteral("lxd")}};
    for (const auto &[cli, label] : clis) {
        if (!haveCmd(cli))
            continue;
        const QString js = runCmd(cli,
            {QStringLiteral("list"), QStringLiteral("--format"), QStringLiteral("json")}, 10000);
        for (const QJsonValue &v : QJsonDocument::fromJson(js.toUtf8()).array()) {
            const QJsonObject c = v.toObject();
            const QString name = c.value(QStringLiteral("name")).toString();
            if (name.isEmpty())
                continue;
            const QJsonObject cfg = c.value(QStringLiteral("config")).toObject();
            const QString image = cfg.value(QStringLiteral("image.description")).toString();
            out.append({name, label, image, c.value(QStringLiteral("status")).toString(),
                        QStringLiteral("—"), QString(), QString(),
                        c.value(QStringLiteral("created_at")).toString()});
        }
    }
}

QString desktopValue(const QString &path, const QString &key)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    bool inEntry = false;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.startsWith(QLatin1Char('['))) inEntry = (line == QLatin1String("[Desktop Entry]"));
        else if (inEntry && line.startsWith(key + QLatin1Char('=')))
            return line.mid(key.size() + 1);
    }
    return {};
}

} // namespace

QVector<QStringList> Fetchers::timers()
{
    QVector<QStringList> out;
    SystemdManager sys(SystemdManager::System), usr(SystemdManager::User);
    appendTimers(sys, out);
    appendTimers(usr, out);
    return out;
}

QVector<QStringList> Fetchers::mounts()
{
    QVector<QStringList> out;
    QHash<QString, QJsonObject> bindln;
    if (haveCmd(QStringLiteral("bindln"))) {
        const QString bj = runCmd(QStringLiteral("bindln"),
            {QStringLiteral("ls"), QStringLiteral("--json")}, 5000);
        for (const QJsonValue &v : QJsonDocument::fromJson(bj.toUtf8()).array()) {
            const QJsonObject o = v.toObject();
            const QString t = o.value(QStringLiteral("target")).toString();
            if (!t.isEmpty()) bindln.insert(t, o);
        }
    }
    const QString fj = runCmd(QStringLiteral("findmnt"), {QStringLiteral("--json")});
    QSet<QString> seen;
    flattenMounts(QJsonDocument::fromJson(fj.toUtf8()).object()
                      .value(QStringLiteral("filesystems")).toArray(),
                  bindln, seen, out);
    // bindln links that are defined but not currently mounted.
    for (auto it = bindln.constBegin(); it != bindln.constEnd(); ++it) {
        if (seen.contains(it.key()))
            continue;
        const QJsonObject m = it.value();
        out.append({it.key(), m.value(QStringLiteral("source")).toString(),
                    QStringLiteral("none"), QStringLiteral("bindln"),
                    QStringLiteral("not mounted (%1, %2)")
                        .arg(m.value(QStringLiteral("active")).toString(QStringLiteral("?")),
                             m.value(QStringLiteral("enabled")).toString(QStringLiteral("?"))),
                    QStringLiteral("bindln"), QStringLiteral("bind")});
    }
    return out;
}

QVector<QStringList> Fetchers::ports()
{
    QVector<QStringList> out;
    static const QRegularExpression userRe(
        QStringLiteral("users:\\(\\(\"([^\"]*)\",pid=(\\d+)"));
    const QString ss = runCmd(QStringLiteral("ss"), {QStringLiteral("-tlnpH")});
    for (const QString &line : ss.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
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
        out.append({QStringLiteral("tcp"), addr, port, pid, proc, path, cmd});
    }
    return out;
}

QVector<QStringList> Fetchers::autostart()
{
    QVector<QStringList> out;
    QSet<QString> running;
    for (const QString &c : runCmd(QStringLiteral("ps"), {QStringLiteral("-eo"), QStringLiteral("comm=")})
                                .split(QLatin1Char('\n'), Qt::SkipEmptyParts))
        running.insert(c.trimmed());

    const QVector<QPair<QString, QString>> dirs = {
        {QDir::homePath() + QStringLiteral("/.config/autostart"), QStringLiteral("User")},
        {QStringLiteral("/etc/xdg/autostart"), QStringLiteral("System")}};
    for (const auto &[dirPath, scope] : dirs) {
        QDir dir(dirPath);
        if (!dir.exists())
            continue;
        for (const QFileInfo &fi : dir.entryInfoList({QStringLiteral("*.desktop")}, QDir::Files)) {
            const QString path = fi.absoluteFilePath();
            const QString name = desktopValue(path, QStringLiteral("Name"));
            const QString exec = desktopValue(path, QStringLiteral("Exec"));
            const bool hidden = desktopValue(path, QStringLiteral("Hidden")).toLower() == QLatin1String("true");
            const QString exeBase = exec.section(QLatin1Char(' '), 0, 0).section(QLatin1Char('/'), -1);
            out.append({name.isEmpty() ? fi.fileName() : name, exec, scope,
                        hidden ? QStringLiteral("No") : QStringLiteral("Yes"),
                        running.contains(exeBase) ? QStringLiteral("Yes") : QStringLiteral("No"),
                        path});
        }
    }
    return out;
}

QVector<QStringList> Fetchers::snaps()
{
    QVector<QStringList> out;
    if (!haveCmd(QStringLiteral("snap")))
        return out;
    const QStringList lines = runCmd(QStringLiteral("snap"), {QStringLiteral("services")})
                                  .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (int i = 1; i < lines.size(); ++i) { // skip header
        const QStringList p = lines.at(i).split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (p.size() < 3)
            continue;
        const QString svc = p.at(0);
        out.append({svc.section(QLatin1Char('.'), 0, 0), svc, p.at(1), p.at(2),
                    p.size() > 3 ? p.mid(3).join(QLatin1Char(' ')) : QString()});
    }
    return out;
}

QVector<QStringList> Fetchers::containers()
{
    QVector<QStringList> out;
    appendDockerLike(QStringLiteral("docker"), out);
    appendDockerLike(QStringLiteral("podman"), out);
    appendIncusLxd(out);
    return out;
}

namespace {

// Parse one crontab's text into rows tagged with @p user.
void appendCronRows(const QString &user, const QString &text, QVector<QStringList> &out)
{
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.contains(QLatin1Char('=')))
            continue;
        if (line.startsWith(QLatin1Char('@'))) {
            const QString sched = line.section(QRegularExpression(QStringLiteral("\\s+")), 0, 0);
            const QString cmd = line.section(QRegularExpression(QStringLiteral("\\s+")), 1);
            out.append({user, sched, QString(), QString(), QString(), QString(), cmd});
            continue;
        }
        const QStringList p = line.split(QRegularExpression(QStringLiteral("\\s+")));
        if (p.size() < 6)
            continue;
        out.append({user, p.at(0), p.at(1), p.at(2), p.at(3), p.at(4), p.mid(5).join(QLatin1Char(' '))});
    }
}

// Crontab text for @p user: the current user reads directly, others via KAuth.
QString cronText(const QString &user)
{
    if (user == Cron::currentUser())
        return runCmd(QStringLiteral("crontab"), {QStringLiteral("-l")});
    return Auth::cronList(user, nullptr);
}

} // namespace

QVector<QStringList> Fetchers::cronFor(const QString &user)
{
    QVector<QStringList> out;
    // Aggregate view: every crontab-bearing user's jobs (requires prior elevation).
    if (user == Cron::kAllUsers) {
        for (const Auth::CronUser &u : Auth::cronUsers(nullptr))
            appendCronRows(u.user, cronText(u.user), out);
        return out;
    }
    appendCronRows(user, cronText(user), out);
    return out;
}

QVector<QStringList> Fetchers::sessions()
{
    QVector<QStringList> out;
    if (!haveCmd(QStringLiteral("loginctl")))
        return out;
    // List session ids, then query details for each.
    const QString list = runCmd(QStringLiteral("loginctl"),
        {QStringLiteral("list-sessions"), QStringLiteral("--no-legend")});
    for (const QString &line : list.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString id = line.trimmed().section(QRegularExpression(QStringLiteral("\\s+")), 0, 0);
        if (id.isEmpty())
            continue;
        QHash<QString, QString> kv;
        const QString show = runCmd(QStringLiteral("loginctl"),
            {QStringLiteral("show-session"), id, QStringLiteral("-p"), QStringLiteral("Id"),
             QStringLiteral("-p"), QStringLiteral("Name"), QStringLiteral("-p"), QStringLiteral("User"),
             QStringLiteral("-p"), QStringLiteral("Seat"), QStringLiteral("-p"), QStringLiteral("TTY"),
             QStringLiteral("-p"), QStringLiteral("Type"), QStringLiteral("-p"), QStringLiteral("State"),
             QStringLiteral("-p"), QStringLiteral("Remote"), QStringLiteral("-p"), QStringLiteral("RemoteHost")});
        for (const QString &kvline : show.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const int eq = kvline.indexOf(QLatin1Char('='));
            if (eq > 0) kv.insert(kvline.left(eq), kvline.mid(eq + 1));
        }
        out.append({kv.value(QStringLiteral("Id"), id), kv.value(QStringLiteral("Name")),
                    kv.value(QStringLiteral("User")), kv.value(QStringLiteral("Seat")),
                    kv.value(QStringLiteral("TTY")), kv.value(QStringLiteral("Type")),
                    kv.value(QStringLiteral("State")), kv.value(QStringLiteral("RemoteHost"))});
    }
    return out;
}
