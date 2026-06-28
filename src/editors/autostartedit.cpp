// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#include "autostartedit.h"

#include <KConfig>
#include <KConfigGroup>
#include <KLocalizedString>

#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QLineEdit>
#include <QRegularExpression>

/*┌─────────────────╮
  | Autostart CRUD  |
  └─────────────────╯*/
//region Autostart CRUD
namespace {

QString userDir()
{
    return QDir::homePath() + QStringLiteral("/.config/autostart/");
}

const char *kGroup = "Desktop Entry";

} // namespace

QString Autostart::readComment(const QString &file)
{
    KConfig cfg(file, KConfig::SimpleConfig);
    return cfg.group(QString::fromLatin1(kGroup)).readEntry("Comment", QString());
}

bool Autostart::save(const Entry &entry, QString *error)
{
    if (entry.name.isEmpty() || entry.exec.isEmpty()) {
        if (error) *error = i18n("Name and command are required.");
        return false;
    }
    QString file = entry.file;
    const bool creating = file.isEmpty();
    if (creating) {
        QString slug = entry.name.toLower();
        slug.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
        slug = slug.trimmed();
        if (slug.isEmpty())
            slug = QStringLiteral("autostart");
        QDir().mkpath(userDir());
        file = userDir() + slug + QStringLiteral(".desktop");
    } else if (!QFileInfo(file).isWritable()) {
        if (error) *error = i18n("This is a system-wide entry and can't be edited here. "
                                 "Use Toggle to disable it for your user.");
        return false;
    }

    KConfig cfg(file, KConfig::SimpleConfig);
    KConfigGroup g = cfg.group(QString::fromLatin1(kGroup));
    g.writeEntry("Type", QStringLiteral("Application"));
    g.writeEntry("Name", entry.name);
    g.writeEntry("Exec", entry.exec);
    g.writeEntry("Comment", entry.comment);
    if (creating) {
        g.writeEntry("Hidden", false);
        g.writeEntry("X-GNOME-Autostart-enabled", true);
    }
    cfg.sync();
    return true;
}

bool Autostart::remove(const QString &file, QString *error)
{
    if (!QFileInfo(file).isWritable()) {
        if (error) *error = i18n("Can't delete a system-wide entry. Use Toggle to disable it.");
        return false;
    }
    if (QFile::remove(file))
        return true;
    if (error) *error = i18n("Failed to delete %1.", file);
    return false;
}

bool Autostart::toggle(const QString &file, QString *error)
{
    const QString group = QString::fromLatin1(kGroup);
    const bool isUser = file.startsWith(userDir());
    if (isUser) {
        KConfig cfg(file, KConfig::SimpleConfig);
        KConfigGroup g = cfg.group(group);
        g.writeEntry("Hidden", !g.readEntry("Hidden", false));
        cfg.sync();
        return true;
    }

    // System entry → write/update a user override that shadows it.
    const QString override = userDir() + QFileInfo(file).fileName();
    QDir().mkpath(userDir());
    KConfig cfg(override, KConfig::SimpleConfig);
    KConfigGroup g = cfg.group(group);
    if (QFileInfo::exists(override) && g.hasKey("Hidden")) {
        g.writeEntry("Hidden", !g.readEntry("Hidden", false));
    } else {
        KConfig sys(file, KConfig::SimpleConfig);
        KConfigGroup sg = sys.group(group);
        g.writeEntry("Type", sg.readEntry("Type", QStringLiteral("Application")));
        g.writeEntry("Name", sg.readEntry("Name", QFileInfo(file).fileName()));
        g.writeEntry("Exec", sg.readEntry("Exec", QString()));
        g.writeEntry("Hidden", true); // first toggle on a system entry disables it
    }
    cfg.sync();
    Q_UNUSED(error)
    return true;
}
//endregion

/*┌─────────────────╮
  | Edit dialog     |
  └─────────────────╯*/
//region Edit dialog
AutostartEditDialog::AutostartEditDialog(QWidget *parent, const Autostart::Entry &entry)
    : QDialog(parent), mFile(entry.file)
{
    setWindowTitle(entry.file.isEmpty() ? i18nc("@title:window", "Add Autostart Entry")
                                        : i18nc("@title:window", "Edit Autostart Entry"));
    resize(520, 0);
    auto *form = new QFormLayout(this);

    mName = new QLineEdit(entry.name, this);
    mExec = new QLineEdit(entry.exec, this);
    mComment = new QLineEdit(entry.comment, this);
    form->addRow(i18n("Name:"), mName);
    form->addRow(i18n("Command:"), mExec);
    form->addRow(i18n("Comment:"), mComment);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(box);
}

Autostart::Entry AutostartEditDialog::entry() const
{
    return {mName->text().trimmed(), mExec->text().trimmed(), mComment->text().trimmed(), mFile};
}
//endregion
