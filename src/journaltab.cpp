// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#include "journaltab.h"

#include "util/proc.h"

#include <KLocalizedString>

#include <QComboBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QVBoxLayout>

JournalTab::JournalTab(QWidget *parent) : QWidget(parent)
{
    mScope = new QComboBox(this);
    mScope->addItems({i18nc("@item journal scope", "System"), i18nc("@item journal scope", "User")});
    connect(mScope, &QComboBox::currentIndexChanged, this, &JournalTab::refresh);

    auto *refreshBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")),
                                       i18nc("@action:button", "Refresh"), this);
    connect(refreshBtn, &QPushButton::clicked, this, &JournalTab::refresh);

    mSearch = new QLineEdit(this);
    mSearch->setPlaceholderText(i18nc("@info:placeholder", "Filter lines…"));
    mSearch->setClearButtonEnabled(true);
    connect(mSearch, &QLineEdit::textChanged, this, &JournalTab::applyFilter);

    mView = new QPlainTextEdit(this);
    mView->setReadOnly(true);
    mView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    mView->setLineWrapMode(QPlainTextEdit::NoWrap);

    auto *top = new QHBoxLayout;
    top->addWidget(mScope);
    top->addWidget(refreshBtn);
    top->addWidget(mSearch, 1); // search fills the remaining width

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(top);
    layout->addWidget(mView);

    refresh();
}

void JournalTab::refresh()
{
    QStringList args = {QStringLiteral("-n"), QStringLiteral("500"), QStringLiteral("--no-pager")};
    if (mScope->currentIndex() == 1)
        args.prepend(QStringLiteral("--user"));
    mFull = runCmd(QStringLiteral("journalctl"), args, 10000);
    applyFilter();
}

void JournalTab::applyFilter()
{
    const QString needle = mSearch->text();
    if (needle.isEmpty()) {
        mView->setPlainText(mFull);
    } else {
        QStringList kept;
        for (const QString &line : mFull.split(QLatin1Char('\n')))
            if (line.contains(needle, Qt::CaseInsensitive))
                kept << line;
        mView->setPlainText(kept.join(QLatin1Char('\n')));
    }
    mView->verticalScrollBar()->setValue(mView->verticalScrollBar()->maximum());
}
