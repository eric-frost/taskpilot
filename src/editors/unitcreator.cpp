// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#include "unitcreator.h"

#include "util/auth.h"
#include "util/proc.h"

#include <KLocalizedString>
#include <KMessageBox>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

UnitCreatorDialog::UnitCreatorDialog(QWidget *parent, const QString &type) : QDialog(parent)
{
    setWindowTitle(i18nc("@title:window", "Create Unit"));
    resize(620, 560);
    auto *layout = new QVBoxLayout(this);

    auto *form = new QFormLayout;
    mType = new QComboBox(this);
    mType->addItems({QStringLiteral("service"), QStringLiteral("timer")});
    mType->setCurrentText(type == QLatin1String("timer") ? QStringLiteral("timer") : QStringLiteral("service"));
    mName = new QLineEdit(this);
    mName->setPlaceholderText(QStringLiteral("my-task"));
    mDescription = new QLineEdit(this);
    mExec = new QLineEdit(this);
    mExec->setPlaceholderText(QStringLiteral("/usr/bin/my-command"));
    mRestart = new QComboBox(this);
    mRestart->addItems({QStringLiteral("no"), QStringLiteral("on-failure"), QStringLiteral("always"),
                        QStringLiteral("on-abnormal")});
    mOnCalendar = new QLineEdit(this);
    mOnCalendar->setPlaceholderText(QStringLiteral("*-*-* 00:00:00  (timers only)"));
    mScope = new QComboBox(this);
    mScope->addItems({i18n("User"), i18n("System")});

    form->addRow(i18n("Type:"), mType);
    form->addRow(i18n("Name:"), mName);
    form->addRow(i18n("Description:"), mDescription);
    form->addRow(i18n("ExecStart:"), mExec);
    form->addRow(i18n("Restart:"), mRestart);
    form->addRow(i18n("OnCalendar:"), mOnCalendar);
    form->addRow(i18n("Scope:"), mScope);
    layout->addLayout(form);

    layout->addWidget(new QLabel(i18n("Preview:"), this));
    mPreview = new QPlainTextEdit(this);
    mPreview->setReadOnly(true);
    mPreview->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(mPreview);

    for (QComboBox *c : {mType, mRestart, mScope})
        connect(c, &QComboBox::currentTextChanged, this, &UnitCreatorDialog::updatePreview);
    for (QLineEdit *e : {mName, mDescription, mExec, mOnCalendar})
        connect(e, &QLineEdit::textChanged, this, &UnitCreatorDialog::updatePreview);
    updatePreview();

    auto *box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, &UnitCreatorDialog::save);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(box);
}

void UnitCreatorDialog::updatePreview()
{
    const QString desc = mDescription->text().trimmed().isEmpty() ? i18n("My Task")
                                                                  : mDescription->text().trimmed();
    QString text;
    if (mType->currentText() == QLatin1String("service")) {
        const QString wantedBy = mScope->currentIndex() == 0 ? QStringLiteral("default.target")
                                                             : QStringLiteral("multi-user.target");
        text = QStringLiteral("[Unit]\nDescription=%1\n\n[Service]\nExecStart=%2\nRestart=%3\n\n"
                              "[Install]\nWantedBy=%4\n")
                   .arg(desc, mExec->text().trimmed(), mRestart->currentText(), wantedBy);
    } else {
        text = QStringLiteral("[Unit]\nDescription=%1\n\n[Timer]\nOnCalendar=%2\nPersistent=true\n\n"
                              "[Install]\nWantedBy=timers.target\n")
                   .arg(desc, mOnCalendar->text().trimmed());
    }
    mPreview->setPlainText(text);
}

void UnitCreatorDialog::save()
{
    const QString name = mName->text().trimmed();
    if (name.isEmpty()) {
        KMessageBox::error(this, i18n("Enter a unit name."));
        return;
    }
    const QString fileName = name + QLatin1Char('.') + mType->currentText();
    const QString content = mPreview->toPlainText();

    QString error;
    QString savedPath;
    if (mScope->currentIndex() == 0) { // User scope: write directly, no root needed.
        const QString dir = QDir::homePath() + QStringLiteral("/.config/systemd/user");
        QDir().mkpath(dir);
        savedPath = dir + QLatin1Char('/') + fileName;
        QFile f(savedPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            KMessageBox::error(this, f.errorString());
            return;
        }
        f.write(content.toUtf8());
        f.close();
        runCmd(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("daemon-reload")});
    } else { // System scope: write through the KAuth helper.
        savedPath = QStringLiteral("/etc/systemd/system/") + fileName;
        if (!Auth::writeUnit(savedPath, content, &error)) {
            KMessageBox::error(this, error.isEmpty() ? i18n("Failed to save the unit.") : error);
            return;
        }
    }
    KMessageBox::information(this, i18n("Saved to %1", savedPath), i18nc("@title:window", "Unit Created"));
    accept();
}
