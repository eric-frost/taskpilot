// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cronedit.h"

#include "util/auth.h"
#include "util/proc.h"

#include <KLocalizedString>
#include <KMessageBox>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QRegularExpression>
#include <QVBoxLayout>

#include <algorithm>
#include <pwd.h>
#include <unistd.h>

/*┌─────────────────╮
  | Crontab model   |
  └─────────────────╯*/
//region Crontab model
namespace {

// One crontab line: either a job, or a passthrough line (env/blank/orphan comment).
struct Item {
    bool isJob = false;
    QString raw;     // passthrough text (no trailing newline)
    Cron::Job job;   // valid when isJob
};

bool isEnvLine(const QString &t)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*\\s*="));
    return re.match(t).hasMatch();
}

// Build the schedule+command line for a job.
QString jobLine(const Cron::Job &j)
{
    if (j.min.startsWith(QLatin1Char('@')))
        return j.min + QLatin1Char(' ') + j.cmd;
    return QStringList{j.min, j.hour, j.dom, j.mon, j.dow, j.cmd}.join(QLatin1Char(' '));
}

// Parse crontab text into ordered items, preserving everything.
QList<Item> parse(const QString &text)
{
    QList<Item> items;
    QString pendingComment; // a "# …" line immediately preceding a job becomes its comment
    bool havePending = false;

    const QStringList lines = text.split(QLatin1Char('\n'));
    auto flushComment = [&] {
        if (havePending) {
            items.append({false, QStringLiteral("# ") + pendingComment, {}});
            havePending = false;
        }
    };
    for (const QString &raw : lines) {
        const QString t = raw.trimmed();
        if (t.isEmpty()) {
            flushComment();
            items.append({false, QString(), {}});
            continue;
        }
        if (t.startsWith(QLatin1Char('#'))) {
            flushComment(); // two comments in a row: first is orphan passthrough
            pendingComment = t.mid(1).trimmed();
            havePending = true;
            continue;
        }
        if (isEnvLine(t)) {
            flushComment();
            items.append({false, t, {}});
            continue;
        }
        // A schedule line.
        Cron::Job j;
        j.comment = havePending ? pendingComment : QString();
        havePending = false;
        if (t.startsWith(QLatin1Char('@'))) {
            static const QRegularExpression ws(QStringLiteral("\\s+"));
            j.min = t.section(ws, 0, 0);
            j.cmd = t.section(ws, 1);
        } else {
            const QStringList p = t.split(QRegularExpression(QStringLiteral("\\s+")));
            if (p.size() < 6) {
                items.append({false, t, {}}); // malformed: keep verbatim
                continue;
            }
            j.min = p.at(0); j.hour = p.at(1); j.dom = p.at(2);
            j.mon = p.at(3); j.dow = p.at(4); j.cmd = p.mid(5).join(QLatin1Char(' '));
        }
        items.append({true, QString(), j});
    }
    // Drop the single trailing empty line that split() leaves from the final newline.
    if (!items.isEmpty() && !items.last().isJob && items.last().raw.isEmpty())
        items.removeLast();
    return items;
}

QString serialize(const QList<Item> &items)
{
    QStringList out;
    for (const Item &it : items) {
        if (!it.isJob) {
            out << it.raw;
            continue;
        }
        if (!it.job.comment.isEmpty())
            out << QStringLiteral("# ") + it.job.comment;
        out << jobLine(it.job);
    }
    return out.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

// Crontab text for @p user: current user reads directly, others via KAuth.
QString readText(const QString &user, QString *error)
{
    if (user == Cron::currentUser())
        return runCmd(QStringLiteral("crontab"), {QStringLiteral("-l")});
    return Auth::cronList(user, error);
}

// Write @p items to @p user's crontab: current user directly, others via KAuth.
bool write(const QString &user, const QList<Item> &items, QString *error)
{
    const QString text = serialize(items);
    if (user != Cron::currentUser())
        return Auth::cronSave(user, text, error);

    QProcess p;
    p.start(QStringLiteral("crontab"), {QStringLiteral("-")});
    if (!p.waitForStarted(2000)) {
        if (error) *error = i18n("Could not run crontab.");
        return false;
    }
    p.write(text.toUtf8());
    p.closeWriteChannel();
    p.waitForFinished(8000);
    if (p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0)
        return true;
    if (error)
        *error = QString::fromUtf8(p.readAllStandardError()).trimmed();
    return false;
}

} // namespace
//endregion

/*┌─────────────────╮
  | Cron CRUD       |
  └─────────────────╯*/
//region Cron CRUD
QString Cron::currentUser()
{
    // Use the real uid (matches the helper's getpwnam view); $USER can be stale
    // under su/sudo -E, which would misroute the user's own crontab through KAuth.
    if (const struct passwd *pw = getpwuid(getuid()))
        return QString::fromLocal8Bit(pw->pw_name);
    return qEnvironmentVariable("USER", QStringLiteral("me"));
}

std::optional<Cron::Job> Cron::find(const QString &user, const Job &match)
{
    for (const Item &it : parse(readText(user, nullptr)))
        if (it.isJob && it.job.sameAs(match))
            return it.job;
    return std::nullopt;
}

bool Cron::add(const QString &user, const Job &job, QString *error)
{
    QList<Item> items = parse(readText(user, error));
    items.append({true, QString(), job});
    return write(user, items, error);
}

bool Cron::update(const QString &user, const Job &match, const Job &replacement, QString *error)
{
    QList<Item> items = parse(readText(user, error));
    for (Item &it : items)
        if (it.isJob && it.job.sameAs(match)) {
            it.job = replacement;
            return write(user, items, error);
        }
    if (error) *error = i18n("The cron job was not found (it may have changed).");
    return false;
}

bool Cron::remove(const QString &user, const QList<Job> &jobs, QString *error)
{
    QList<Item> items = parse(readText(user, error));
    const auto matches = [&](const Job &j) {
        for (const Job &m : jobs)
            if (j.sameAs(m))
                return true;
        return false;
    };
    items.erase(std::remove_if(items.begin(), items.end(),
                               [&](const Item &it) { return it.isJob && matches(it.job); }),
                items.end());
    return write(user, items, error);
}
//endregion

/*┌─────────────────╮
  | Edit dialog     |
  └─────────────────╯*/
//region Edit dialog
namespace {

// (label, common values offered in the editable combo) per schedule field.
const struct {
    const char *label;
    QStringList values;
} kFields[5] = {
    {"Minute", {QStringLiteral("*"), QStringLiteral("0"), QStringLiteral("*/5"), QStringLiteral("*/15"), QStringLiteral("30")}},
    {"Hour", {QStringLiteral("*"), QStringLiteral("0"), QStringLiteral("6"), QStringLiteral("12"), QStringLiteral("*/6")}},
    {"Day of Month", {QStringLiteral("*"), QStringLiteral("1"), QStringLiteral("15")}},
    {"Month", {QStringLiteral("*"), QStringLiteral("1"), QStringLiteral("6"), QStringLiteral("12")}},
    {"Day of Week", {QStringLiteral("*"), QStringLiteral("0"), QStringLiteral("1-5"), QStringLiteral("6")}},
};

// (preset name, five field values).
const struct {
    const char *name;
    const char *vals[5];
} kPresets[] = {
    {"Every minute", {"*", "*", "*", "*", "*"}},
    {"Every 5 minutes", {"*/5", "*", "*", "*", "*"}},
    {"Every 15 minutes", {"*/15", "*", "*", "*", "*"}},
    {"Every hour", {"0", "*", "*", "*", "*"}},
    {"Every 6 hours", {"0", "*/6", "*", "*", "*"}},
    {"Daily at midnight", {"0", "0", "*", "*", "*"}},
    {"Daily at 6 AM", {"0", "6", "*", "*", "*"}},
    {"Weekdays at 9 AM", {"0", "9", "*", "*", "1-5"}},
    {"Weekly (Sun midnight)", {"0", "0", "*", "*", "0"}},
    {"Monthly (1st midnight)", {"0", "0", "1", "*", "*"}},
};

} // namespace

CronEditDialog::CronEditDialog(QWidget *parent, const Cron::Job &job, const QStringList &users,
                               const QString &currentUser)
    : QDialog(parent)
{
    setWindowTitle(job.cmd.isEmpty() ? i18nc("@title:window", "New Cron Job")
                                     : i18nc("@title:window", "Edit Cron Job"));
    resize(620, 0);
    auto *layout = new QVBoxLayout(this);

    // 0. Optional user selector (the "All" view's Add — pick whose crontab).
    if (!users.isEmpty()) {
        mUser = new QComboBox(this);
        mUser->addItems(users);
        if (!currentUser.isEmpty() && users.contains(currentUser))
            mUser->setCurrentText(currentUser);
        auto *userRow = new QGridLayout;
        userRow->addWidget(new QLabel(i18n("Run as user:"), this), 0, 0);
        userRow->addWidget(mUser, 0, 1);
        layout->addLayout(userRow);
    }

    // 1. Preset.
    auto *preset = new QComboBox(this);
    preset->addItem(i18n("Custom"));
    for (const auto &p : kPresets)
        preset->addItem(QString::fromUtf8(p.name));
    auto *presetRow = new QGridLayout;
    presetRow->addWidget(new QLabel(i18n("Preset:"), this), 0, 0);
    presetRow->addWidget(preset, 0, 1);
    layout->addLayout(presetRow);

    // 2. Five schedule fields.
    auto *grid = new QGridLayout;
    const QString init[5] = {job.min, job.hour, job.dom, job.mon, job.dow};
    for (int i = 0; i < 5; ++i) {
        grid->addWidget(new QLabel(i18n(kFields[i].label), this), 0, i);
        mFields[i] = new QComboBox(this);
        mFields[i]->setEditable(true);
        mFields[i]->addItems(kFields[i].values);
        mFields[i]->setCurrentText(init[i].isEmpty() ? QStringLiteral("*") : init[i]);
        grid->addWidget(mFields[i], 1, i);
    }
    layout->addLayout(grid);

    connect(preset, &QComboBox::currentIndexChanged, this, [this, preset](int idx) {
        if (idx <= 0)
            return; // "Custom"
        const auto &vals = kPresets[idx - 1].vals;
        for (int i = 0; i < 5; ++i)
            mFields[i]->setCurrentText(QString::fromUtf8(vals[i]));
    });

    // 3. Command + comment.
    layout->addWidget(new QLabel(i18n("Command:"), this));
    mCmd = new QLineEdit(job.cmd, this);
    layout->addWidget(mCmd);
    layout->addWidget(new QLabel(i18n("Comment (optional):"), this));
    mComment = new QLineEdit(job.comment, this);
    layout->addWidget(mComment);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, [this] {
        if (mCmd->text().trimmed().isEmpty()) {
            KMessageBox::error(this, i18n("The command can't be empty.")); // else it round-trips lossily
            return;
        }
        accept();
    });
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(box);
}

Cron::Job CronEditDialog::job() const
{
    Cron::Job j;
    j.min = mFields[0]->currentText().trimmed();
    j.hour = mFields[1]->currentText().trimmed();
    j.dom = mFields[2]->currentText().trimmed();
    j.mon = mFields[3]->currentText().trimmed();
    j.dow = mFields[4]->currentText().trimmed();
    j.cmd = mCmd->text().trimmed();
    j.comment = mComment->text().trimmed();
    // A bare "@reboot"-style schedule lives in the minute field; blank the rest.
    if (j.min.startsWith(QLatin1Char('@')))
        j.hour = j.dom = j.mon = j.dow = QString();
    return j;
}

QString CronEditDialog::user() const
{
    return mUser ? mUser->currentText() : QString();
}
//endregion
