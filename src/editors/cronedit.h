// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDialog>
#include <QString>
#include <optional>

class QComboBox;
class QLineEdit;

/** Read/edit any user's crontab. The current user uses `crontab -l`/`crontab -`;
    other users go through the KAuth helper. Env-var lines, blank lines and
    unattached comments are preserved — only the targeted job block changes. */
namespace Cron {

struct Job {
    QString min, hour, dom, mon, dow, cmd, comment;
    // Two jobs are "the same" if schedule + command match (comment ignored), so
    // a selected table row (which has no comment) can find its crontab entry.
    bool sameAs(const Job &o) const
    {
        return min == o.min && hour == o.hour && dom == o.dom && mon == o.mon
            && dow == o.dow && cmd == o.cmd;
    }
};

/** The current process's username; other users route through KAuth. */
QString currentUser();

/** Sentinel selecting the aggregate "all users" view (control char → can never
    collide with a real username). */
inline const QString kAllUsers = QStringLiteral("\x01""all");

/** Full job (incl. comment) in @p user's crontab matching @p match, else nullopt. */
std::optional<Job> find(const QString &user, const Job &match);

bool add(const QString &user, const Job &job, QString *error);
bool update(const QString &user, const Job &match, const Job &replacement, QString *error);
bool remove(const QString &user, const QList<Job> &jobs, QString *error);

} // namespace Cron

/** Form for one cron job: preset + five schedule fields + command + comment.
    When @p users is non-empty a "Run as user" selector is shown (used by the
    Cron "All" view's Add). */
class CronEditDialog : public QDialog {
    Q_OBJECT
public:
    explicit CronEditDialog(QWidget *parent, const Cron::Job &job = {},
                            const QStringList &users = {}, const QString &currentUser = {});
    Cron::Job job() const;

    /** The chosen user when a selector is shown, else an empty string. */
    QString user() const;

private:
    QComboBox *mFields[5];
    QLineEdit *mCmd;
    QLineEdit *mComment;
    QComboBox *mUser = nullptr;
};
