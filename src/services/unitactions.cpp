// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#include "unitactions.h"

#include "systemd/systemdmanager.h"
#include "util/dialogs.h"
#include "util/proc.h"

#include <KLocalizedString>

#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

bool isUser(const QString &scope)
{
    return scope == QLatin1String("User");
}

// systemctl/journalctl args with --user prepended for user-scope units.
QStringList scoped(const QString &scope, const QStringList &args)
{
    QStringList out;
    if (isUser(scope))
        out << QStringLiteral("--user");
    return out + args;
}

// The unit's fragment (.service/.timer file) path, or empty.
QString fragmentPath(const QString &id, const QString &scope)
{
    const QString out = runCmd(QStringLiteral("systemctl"),
        scoped(scope, {QStringLiteral("show"), QStringLiteral("-p"), QStringLiteral("FragmentPath"), id}));
    return out.section(QLatin1Char('='), 1).trimmed();
}

} // namespace

void UnitActions::logs(QWidget *parent, const QString &id, const QString &scope)
{
    const QString text = runCmd(QStringLiteral("journalctl"),
        scoped(scope, {QStringLiteral("-u"), id, QStringLiteral("-n"), QStringLiteral("200"),
                       QStringLiteral("--no-pager")}), 12000);
    Dialogs::showText(parent, i18nc("@title:window", "Logs: %1", id),
                      text.isEmpty() ? i18n("No logs found (or missing permissions).") : text, true);
}

void UnitActions::dependencies(QWidget *parent, const QString &id, const QString &scope)
{
    const QString text = runCmd(QStringLiteral("systemctl"),
        scoped(scope, {QStringLiteral("list-dependencies"), id}), 12000);
    Dialogs::showText(parent, i18nc("@title:window", "Dependencies: %1", id), text);
}

void UnitActions::editFile(QWidget *parent, const QString &id, const QString &scope)
{
    Q_UNUSED(parent)
    const QString path = fragmentPath(id, scope);
    if (!path.isEmpty())
        QProcess::startDetached(QStringLiteral("xdg-open"), {path});
}

void UnitActions::openLocation(QWidget *parent, const QString &id, const QString &scope)
{
    Q_UNUSED(parent)
    const QString path = fragmentPath(id, scope);
    if (!path.isEmpty())
        QProcess::startDetached(QStringLiteral("xdg-open"), {QFileInfo(path).absolutePath()});
}

void UnitActions::envFiles(QWidget *parent, const QString &id, const QString &scope)
{
    const QString out = runCmd(QStringLiteral("systemctl"),
        scoped(scope, {QStringLiteral("show"), QStringLiteral("-p"),
                       QStringLiteral("EnvironmentFiles,Environment"), id}));
    QString content = out.trimmed().isEmpty()
        ? i18n("No environment files or variables configured.") : out.trimmed();
    // Inline the referenced env files' contents, as the Python version did.
    for (const QString &line : out.split(QLatin1Char('\n'))) {
        if (!line.startsWith(QLatin1String("EnvironmentFiles=")))
            continue;
        const QString path = line.section(QLatin1Char('='), 1).trimmed().section(QLatin1Char(' '), 0, 0);
        if (path.isEmpty())
            continue;
        QFile f(path);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            content += QStringLiteral("\n\n--- %1 ---\n").arg(path) + QString::fromUtf8(f.readAll());
        else
            content += QStringLiteral("\n\n--- %1: cannot read ---").arg(path);
    }
    Dialogs::showText(parent, i18nc("@title:window", "Environment: %1", id), content);
}

bool UnitActions::lifecycle(const QString &verb, const QString &id, const QString &scope, QString *error)
{
    QProcess p;
    p.start(QStringLiteral("systemctl"), scoped(scope, {verb, id}));
    if (!p.waitForStarted(2000)) {
        if (error) *error = i18n("Could not run systemctl.");
        return false;
    }
    p.waitForFinished(15000);
    if (p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0)
        return true;
    if (error)
        *error = QString::fromUtf8(p.readAllStandardError()).trimmed();
    return false;
}

void UnitActions::startupBlame(QWidget *parent)
{
    const QString text = runCmd(QStringLiteral("systemd-analyze"),
                                {QStringLiteral("blame"), QStringLiteral("--no-pager")}, 12000);
    Dialogs::showText(parent, i18nc("@title:window", "Startup Time (systemd-analyze blame)"),
                      text.isEmpty() ? i18n("systemd-analyze produced no output.") : text);
}

void UnitActions::failedUnits(QWidget *parent)
{
    auto *dlg = new QDialog(parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(i18nc("@title:window", "Failed Units"));
    dlg->resize(780, 480);
    auto *layout = new QVBoxLayout(dlg);

    auto *view = new QPlainTextEdit(dlg);
    view->setReadOnly(true);
    view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    layout->addWidget(view);

    auto reload = [view] {
        const QStringList args = {QStringLiteral("list-units"), QStringLiteral("--state=failed"),
                                  QStringLiteral("--no-pager"), QStringLiteral("--no-legend")};
        QString text = runCmd(QStringLiteral("systemctl"), args);
        const QString user = runCmd(QStringLiteral("systemctl"), QStringList{QStringLiteral("--user")} + args);
        if (!user.trimmed().isEmpty())
            text += QStringLiteral("\n--- user ---\n") + user;
        view->setPlainText(text.trimmed().isEmpty() ? i18n("No failed units.") : text);
    };
    reload();

    auto *box = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    auto *reset = box->addButton(i18nc("@action:button", "Reset All Failed"), QDialogButtonBox::ActionRole);
    QObject::connect(box, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    QObject::connect(reset, &QPushButton::clicked, dlg, [reload] {
        QString err;
        SystemdManager system(SystemdManager::System);
        system.resetFailed(err);
        SystemdManager user(SystemdManager::User);
        user.resetFailed(err);
        reload();
    });
    layout->addWidget(box);
    dlg->show();
}
