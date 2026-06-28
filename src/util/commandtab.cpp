// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#include "commandtab.h"

#include "rowtablemodel.h"
#include "util/icons.h"

#include <KLocalizedString>

#include <QAction>
#include <QApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QMenu>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QToolBar>
#include <QVBoxLayout>
#include <QtConcurrentRun>

CommandTab::CommandTab(const QStringList &headers, int statusColumn, Fetcher fetcher,
                       const QVector<RowAction> &actions, QWidget *parent)
    : QWidget(parent), mFetcher(std::move(fetcher))
{
    mModel = new RowTableModel(headers, statusColumn, this);
    mProxy = new QSortFilterProxyModel(this);
    mProxy->setSourceModel(mModel);
    mProxy->setFilterKeyColumn(-1);
    mProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    mProxy->setSortCaseSensitivity(Qt::CaseInsensitive);

    mView = new QTableView(this);
    mView->setModel(mProxy);
    mView->setSelectionBehavior(QAbstractItemView::SelectRows);
    mView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    mView->setSortingEnabled(true);
    mView->setAlternatingRowColors(true);
    mView->verticalHeader()->hide();
    mView->horizontalHeader()->setSectionsMovable(true);
    mView->horizontalHeader()->setStretchLastSection(true);

    // Top bar: Refresh + optional bar actions/widgets, then a stretching search.
    mToolbar = new QToolBar(this);
    mToolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    mToolbar->setIconSize(QSize(16, 16)); // tighter than the default ~22px
    mToolbar->setContentsMargins(0, 0, 0, 0);

    // All per-item commands live in the right-click menu, not the top bar.
    mView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(mView, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        if (mActions.isEmpty())
            return;
        QMenu menu(this);
        menu.addActions(mActions);
        menu.exec(mView->viewport()->mapToGlobal(pos));
    });

    mRefreshAct = mToolbar->addAction(
        QIcon::fromTheme(QStringLiteral("view-refresh")), i18nc("@action", "Refresh"));
    mRefreshAct->setShortcut(QKeySequence::Refresh);
    connect(mRefreshAct, &QAction::triggered, this, &CommandTab::refresh);

    // The fetcher runs on a thread pool; this delivers its rows back on the UI thread.
    connect(&mWatcher, &QFutureWatcherBase::finished, this, [this] {
        mModel->setRows(mWatcher.future().result());
        QApplication::restoreOverrideCursor();
        mRefreshAct->setEnabled(true);
        if (mPending)
            refresh(); // a refresh was coalesced while this one ran; honour it now
    });

    // Per-selected-row actions go straight to the context menu.
    for (const RowAction &a : actions)
        addRowAction(a);

    mSearch = new QLineEdit(this);
    mSearch->setPlaceholderText(i18nc("@info:placeholder", "Search…"));
    mSearch->setClearButtonEnabled(true);
    connect(mSearch, &QLineEdit::textChanged, mProxy, &QSortFilterProxyModel::setFilterFixedString);

    auto *top = new QHBoxLayout;
    top->addWidget(mToolbar);
    top->addWidget(mSearch, 1); // search fills the remaining width

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(top);
    layout->addWidget(mView);

    refresh();
}

void CommandTab::addRowAction(const RowAction &a)
{
    auto *act = new QAction(Icons::resolve(a.icon), a.text, this);
    auto run = a.run;
    connect(act, &QAction::triggered, this, [this, run] {
        const QVector<QStringList> rows = selectedRows();
        for (const QStringList &r : rows)
            run(r);
        refresh();
    });
    mActions << act;
}

void CommandTab::addMenuAction(const QString &text, const QString &icon, std::function<void()> fn)
{
    auto *act = new QAction(Icons::resolve(icon), text, this);
    connect(act, &QAction::triggered, this, [fn = std::move(fn)] { fn(); });
    mActions << act;
}

void CommandTab::addBarAction(const QString &text, const QString &icon, std::function<void()> fn,
                              const QString &tooltip)
{
    QAction *act = mToolbar->addAction(Icons::resolve(icon), text);
    if (!tooltip.isEmpty())
        act->setToolTip(tooltip);
    connect(act, &QAction::triggered, this, [fn = std::move(fn)] { fn(); });
}

void CommandTab::addBarWidget(QWidget *w)
{
    mToolbar->addWidget(w);
}

QVector<QStringList> CommandTab::selectedRows() const
{
    QVector<QStringList> rows;
    const QModelIndexList sel = mView->selectionModel()->selectedRows();
    for (const QModelIndex &idx : sel)
        rows.append(mModel->row(mProxy->mapToSource(idx).row()));
    return rows;
}

void CommandTab::refresh()
{
    if (!mFetcher)
        return;
    if (mWatcher.isRunning()) {
        mPending = true; // coalesce; the finished handler will re-run once
        return;
    }
    mPending = false;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    mRefreshAct->setEnabled(false);
    mWatcher.setFuture(QtConcurrent::run(mFetcher));
}
