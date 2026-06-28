// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDialog>

class QComboBox;
class QLineEdit;
class QPlainTextEdit;

/** Create a new systemd .service or .timer unit, with a live preview. User-scope
    units are written to ~/.config/systemd/user; system-scope units go through the
    KAuth helper. */
class UnitCreatorDialog : public QDialog {
    Q_OBJECT
public:
    explicit UnitCreatorDialog(QWidget *parent, const QString &type = QStringLiteral("service"));

private:
    void updatePreview();
    void save();

    QComboBox *mType;
    QLineEdit *mName;
    QLineEdit *mDescription;
    QLineEdit *mExec;
    QComboBox *mRestart;
    QLineEdit *mOnCalendar;
    QComboBox *mScope;
    QPlainTextEdit *mPreview;
};
