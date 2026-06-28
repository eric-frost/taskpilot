// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dialogs.h"

#include <KLocalizedString>

#include <QDialog>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QVBoxLayout>

void Dialogs::showText(QWidget *parent, const QString &title, const QString &text,
                       bool toBottom, int w, int h)
{
    auto *dlg = new QDialog(parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(title);
    dlg->resize(w, h);

    auto *layout = new QVBoxLayout(dlg);
    auto *view = new QPlainTextEdit(dlg);
    view->setReadOnly(true);
    view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    view->setPlainText(text);
    if (toBottom)
        view->moveCursor(QTextCursor::End);
    layout->addWidget(view);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    QObject::connect(box, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    layout->addWidget(box);

    dlg->show();
}
