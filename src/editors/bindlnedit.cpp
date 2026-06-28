// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#include "bindlnedit.h"

#include "util/auth.h"

#include <KLocalizedString>
#include <KMessageBox>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

BindLinkDialog::BindLinkDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(i18nc("@title:window", "Create Bind Link"));
    resize(720, 0);
    auto *layout = new QVBoxLayout(this);

    auto *form = new QFormLayout;
    mSource = new QLineEdit(this);
    mSource->setPlaceholderText(QStringLiteral("/home/share/notes/projects/todo"));
    mTarget = new QLineEdit(this);
    mTarget->setPlaceholderText(QStringLiteral("/home/projects/foo/agents/todo"));
    form->addRow(i18n("Real folder:"), mSource);
    form->addRow(i18n("Bind-link path:"), mTarget);
    layout->addLayout(form);

    mMkdir = new QCheckBox(i18n("Create real folder if missing"), this);
    mMkdir->setChecked(true);
    mMove = new QCheckBox(i18n("Move existing bind-link folder into the real folder if it's missing"), this);
    mForce = new QCheckBox(i18n("Allow mounting over a non-empty bind-link folder"), this);
    mLate = new QCheckBox(i18n("Mount late instead of during local-filesystem startup"), this);
    layout->addWidget(mMkdir);
    layout->addWidget(mMove);
    layout->addWidget(mForce);
    layout->addWidget(mLate);

    mOutput = new QPlainTextEdit(this);
    mOutput->setReadOnly(true);
    mOutput->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    mOutput->setMaximumHeight(120);
    layout->addWidget(mOutput);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    box->button(QDialogButtonBox::Ok)->setText(i18nc("@action:button", "Create Bind Link"));
    connect(box, &QDialogButtonBox::accepted, this, &BindLinkDialog::create);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(box);
}

void BindLinkDialog::create()
{
    const QString source = mSource->text().trimmed();
    const QString target = mTarget->text().trimmed();
    if (source.isEmpty() || target.isEmpty()) {
        KMessageBox::error(this, i18n("Real folder and bind-link path are required."));
        return;
    }
    QStringList args;
    if (mMkdir->isChecked()) args << QStringLiteral("--mkdir");
    if (mMove->isChecked()) args << QStringLiteral("--move-existing");
    if (mForce->isChecked()) args << QStringLiteral("--force");
    if (mLate->isChecked()) args << QStringLiteral("--late");
    args << source << target;

    QString output, error;
    const bool ok = Auth::bindln(args, &output, &error);
    if (!error.isEmpty()) {
        KMessageBox::error(this, error, i18nc("@title:window", "Bind Link Failed"));
        return;
    }
    mOutput->setPlainText(output);
    if (ok)
        accept();
}
