// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QFutureWatcher>
#include <QStringList>
#include <QVector>
#include <QWidget>
#include <functional>

class RowTableModel;
class QSortFilterProxyModel;
class QTableView;
class QLineEdit;
class QAction;
class QToolBar;

/** A reusable tab: a sortable/filterable table populated by a fetcher callback.
    The top bar holds only Refresh, optional bar actions/widgets (Elevate, the
    Cron user selector), and a stretching search box; all per-item commands live
    in the right-click menu. Used by every tab except Services (bespoke D-Bus)
    and Journal (a log view). */
class CommandTab : public QWidget {
    Q_OBJECT
public:
    using Fetcher = std::function<QVector<QStringList>()>;
    struct RowAction {
        QString text;
        QString icon;
        std::function<void(const QStringList &row)> run; // called once per selected row
    };

    CommandTab(const QStringList &headers, int statusColumn, Fetcher fetcher,
               const QVector<RowAction> &actions = {}, QWidget *parent = nullptr);

    /** Context-menu action that runs once per selected source row, then refreshes
        (Start/Stop/Unmount …). Same as passing it to the constructor. */
    void addRowAction(const RowAction &action);

    /** Context-menu action with an arbitrary callback (Add/Edit/Delete/Logs …).
        Reads the selection itself via selectedRows() when it needs one. */
    void addMenuAction(const QString &text, const QString &icon, std::function<void()> fn);

    /** Inline top-bar button beside Refresh/search (Elevate, Show Docker …).
        @p tooltip, if set, explains what the button does on hover. */
    void addBarAction(const QString &text, const QString &icon, std::function<void()> fn,
                      const QString &tooltip = QString());

    /** Embed an arbitrary widget in the top bar (e.g. the Cron user selector). */
    void addBarWidget(QWidget *w);

    /** The currently selected rows, mapped back to source order. Lets wired-up
        editor actions read the selection (edit/delete/logs/…). */
    QVector<QStringList> selectedRows() const;

    /** Swap the data source used by subsequent refreshes (e.g. switch Ports to
        the elevated fetcher after authorising). Does not refresh on its own. */
    void setFetcher(Fetcher fetcher) { mFetcher = std::move(fetcher); }

public Q_SLOTS:
    void refresh();

private:
    Fetcher mFetcher;
    RowTableModel *mModel;
    QSortFilterProxyModel *mProxy;
    QTableView *mView;
    QLineEdit *mSearch;
    QToolBar *mToolbar = nullptr;
    QAction *mRefreshAct = nullptr;
    QList<QAction *> mActions;                      // per-tab actions, mirrored into the context menu
    QFutureWatcher<QVector<QStringList>> mWatcher; // runs the fetcher off the UI thread
    bool mPending = false;                         // a refresh was requested while one was in flight
};
