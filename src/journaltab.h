// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QWidget>

class QPlainTextEdit;
class QComboBox;
class QLineEdit;

/** Journal tab: shows the last N lines of the systemd journal (system or user),
    with a scope selector, refresh and a search box that filters to matching
    lines. A read-only log view rather than a table. */
class JournalTab : public QWidget {
    Q_OBJECT
public:
    explicit JournalTab(QWidget *parent = nullptr);

public Q_SLOTS:
    void refresh();

private:
    void applyFilter(); // show only lines matching the search box

    QComboBox *mScope;
    QLineEdit *mSearch;
    QPlainTextEdit *mView;
    QString mFull; // unfiltered journal text
};
