// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDialog>
#include <QString>

class QLineEdit;

/** XDG autostart .desktop CRUD in ~/.config/autostart (and toggling system
    entries via a user override). */
namespace Autostart {

struct Entry {
    QString name, exec, comment, file; // file empty => create a new one from name
};

QString readComment(const QString &file);

bool save(const Entry &entry, QString *error);
bool remove(const QString &file, QString *error);
/** Flip enabled/disabled (Hidden=). For a system entry this writes/updates a
    user override under ~/.config/autostart instead of touching the system file. */
bool toggle(const QString &file, QString *error);

} // namespace Autostart

/** Form for one autostart entry: name + command + comment. */
class AutostartEditDialog : public QDialog {
    Q_OBJECT
public:
    explicit AutostartEditDialog(QWidget *parent, const Autostart::Entry &entry = {});
    Autostart::Entry entry() const;

private:
    QString mFile;
    QLineEdit *mName;
    QLineEdit *mExec;
    QLineEdit *mComment;
};
