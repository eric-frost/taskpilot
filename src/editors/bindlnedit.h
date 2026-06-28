// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QPlainTextEdit;

/** Create a bindln-managed bind link (real folder → bind-link path). Runs
    `bindln` as root through the KAuth helper and shows its output inline. */
class BindLinkDialog : public QDialog {
    Q_OBJECT
public:
    explicit BindLinkDialog(QWidget *parent);

private:
    void create();

    QLineEdit *mSource;
    QLineEdit *mTarget;
    QCheckBox *mMkdir;
    QCheckBox *mMove;
    QCheckBox *mForce;
    QCheckBox *mLate;
    QPlainTextEdit *mOutput;
};
