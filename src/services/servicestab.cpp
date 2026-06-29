// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#include "servicestab.h"

#include "editors/unitcreator.h"
#include "servicemodel.h"
#include "systemd/systemdmanager.h"
#include "unitactions.h"
#include "util/icons.h"

#include <KLocalizedString>

#include <QAction>
#include <QCheckBox>
#include <functional>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QToolBar>
#include <QVBoxLayout>

/** Proxy adding scope / status / Hide-Defaults predicates on top of a free-text
    search across all unit fields. Reads the source ServiceModel's UnitInfo
    directly (status & default-ness aren't plain display columns). */
class ServiceFilterProxy : public QSortFilterProxyModel {
public:
    explicit ServiceFilterProxy(ServiceModel *model, QObject *parent = nullptr)
        : QSortFilterProxyModel(parent), mModel(model) {}

    void setText(const QString &t) { mText = t; rerun(); }
    void setScope(const QString &s) { mScope = s; rerun(); }
    void setStatus(const QString &s) { mStatus = s; rerun(); }
    void setHideDefaults(bool h) { mHide = h; rerun(); }

protected:
    bool filterAcceptsRow(int row, const QModelIndex &) const override
    {
        const UnitInfo &u = mModel->unitAt(row);

        if (mScope != QLatin1String("All") && u.scope != mScope)
            return false;
        if (mStatus == QLatin1String("Running") && u.activeState != QLatin1String("active"))
            return false;
        if (mStatus == QLatin1String("Failed") && u.activeState != QLatin1String("failed"))
            return false;
        if (mStatus == QLatin1String("Stopped")
            && (u.activeState == QLatin1String("active") || u.activeState == QLatin1String("failed")))
            return false;
        if (mHide && (u.fragmentPath.startsWith(QLatin1String("/usr/lib/systemd/"))
                      || u.fragmentPath.startsWith(QLatin1String("/lib/systemd/"))))
            return false;
        if (!mText.isEmpty()) {
            const QString hay = u.id + QLatin1Char(' ') + u.description + QLatin1Char(' ')
                + u.scope + QLatin1Char(' ') + u.activeState + QLatin1Char(' ') + u.subState
                + QLatin1Char(' ') + u.unitFileState;
            if (!hay.contains(mText, Qt::CaseInsensitive))
                return false;
        }
        return true;
    }

private:
    // Re-run row filtering (the old invalidateRowsFilter/invalidateFilter are deprecated).
    void rerun()
    {
        beginFilterChange();
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
    }

    ServiceModel *mModel;
    QString mText, mScope = QStringLiteral("All"), mStatus = QStringLiteral("All");
    bool mHide = false;
};

ServicesTab::ServicesTab(QWidget *parent) : QWidget(parent)
{
    mSystem = new SystemdManager(SystemdManager::System, this);
    mUser = new SystemdManager(SystemdManager::User, this);

    mModel = new ServiceModel(this);
    mProxy = new ServiceFilterProxy(mModel, this);
    mProxy->setSourceModel(mModel);
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
    mView->sortByColumn(ServiceModel::Name, Qt::AscendingOrder);

    // 1. Right-click actions (no top buttons — everything lives in the menu).
    auto addMenu = [&](const QString &icon, const QString &text, std::function<void()> fn) {
        auto *act = new QAction(Icons::resolve(icon), text, this);
        connect(act, &QAction::triggered, this, std::move(fn));
        mActions << act;
    };
    auto addSep = [&] {
        auto *sep = new QAction(this);
        sep->setSeparator(true);
        mActions << sep;
    };
    addMenu(QStringLiteral("media-playback-start"), i18nc("@action", "Start"), [this] { runOnSelection(Start); });
    addMenu(QStringLiteral("media-playback-stop"), i18nc("@action", "Stop"), [this] { runOnSelection(Stop); });
    addMenu(QStringLiteral("system-reboot"), i18nc("@action", "Restart"), [this] { runOnSelection(Restart); });
    addSep();
    addMenu(QStringLiteral("dialog-ok-apply"), i18nc("@action", "Enable"), [this] { runOnSelection(Enable); });
    addMenu(QStringLiteral("dialog-cancel"), i18nc("@action", "Disable"), [this] { runOnSelection(Disable); });
    addSep();
    addMenu(QStringLiteral("view-list-text"), i18nc("@action", "Logs"),
            [this] { viewerOnSelection(&UnitActions::logs); });
    addMenu(QStringLiteral("view-list-tree"), i18nc("@action", "Dependencies"),
            [this] { viewerOnSelection(&UnitActions::dependencies); });
    addMenu(QStringLiteral("document-edit"), i18nc("@action", "Edit Unit File(s)"),
            [this] { viewerOnSelection(&UnitActions::editFile); });
    addMenu(QStringLiteral("folder-open"), i18nc("@action", "Open File Location"),
            [this] { viewerOnSelection(&UnitActions::openLocation); });
    addSep();
    addMenu(QStringLiteral("document-properties"), i18nc("@action", "Env Files"),
            [this] { viewerOnSelection(&UnitActions::envFiles); });

    mView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(mView, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QMenu menu(this);
        menu.addActions(mActions);
        menu.exec(mView->viewport()->mapToGlobal(pos));
    });

    // 2. Top bar: Refresh + a stretching search + the filters.
    auto *toolbar = new QToolBar(this);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->setIconSize(QSize(16, 16));
    toolbar->setContentsMargins(0, 0, 0, 0);
    QAction *refreshAct = toolbar->addAction(
        QIcon::fromTheme(QStringLiteral("view-refresh")), i18nc("@action", "Refresh"));
    refreshAct->setShortcut(QKeySequence::Refresh);
    connect(refreshAct, &QAction::triggered, this, &ServicesTab::refresh);

    QAction *addAct = toolbar->addAction(QIcon::fromTheme(QStringLiteral("list-add")),
                                         i18nc("@action", "Add"));
    addAct->setToolTip(i18nc("@info:tooltip", "Create a new systemd service unit."));
    connect(addAct, &QAction::triggered, this, [this] {
        UnitCreatorDialog(this, QStringLiteral("service")).exec();
    });

    mSearch = new QLineEdit(this);
    mSearch->setPlaceholderText(i18nc("@info:placeholder", "Search services…"));
    mSearch->setClearButtonEnabled(true);
    connect(mSearch, &QLineEdit::textChanged, mProxy, &ServiceFilterProxy::setText);

    mScopeFilter = new QComboBox(this);
    mScopeFilter->addItems({i18n("All"), i18n("System"), i18n("User")});
    connect(mScopeFilter, &QComboBox::currentTextChanged, mProxy, &ServiceFilterProxy::setScope);

    mStatusFilter = new QComboBox(this);
    mStatusFilter->addItems({i18n("All"), i18n("Running"), i18n("Stopped"), i18n("Failed")});
    connect(mStatusFilter, &QComboBox::currentTextChanged, mProxy, &ServiceFilterProxy::setStatus);

    mHideDefaults = new QCheckBox(i18n("Hide Defaults"), this);
    mHideDefaults->setToolTip(i18n("Hide distro-provided units (those under /usr/lib/systemd)."));
    connect(mHideDefaults, &QCheckBox::toggled, mProxy, &ServiceFilterProxy::setHideDefaults);

    auto *row = new QHBoxLayout;
    row->addWidget(toolbar);
    row->addWidget(mSearch, 1); // search fills the remaining width
    row->addWidget(mScopeFilter);
    row->addWidget(mStatusFilter);
    row->addWidget(mHideDefaults);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(row);
    layout->addWidget(mView);

    refresh();
}

void ServicesTab::viewerOnSelection(void (*fn)(QWidget *, const QString &, const QString &))
{
    const QModelIndexList rows = mView->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        QMessageBox::information(this, i18nc("@title:window", "No Selection"),
                                i18n("Please select a service first."));
        return;
    }
    const UnitInfo &u = mModel->unitAt(mProxy->mapToSource(rows.first()).row());
    fn(this, u.id, u.scope);
}

void ServicesTab::refresh()
{
    QVector<UnitInfo> all;
    if (mSystem->isAvailable())
        all += mSystem->listUnits(QStringLiteral(".service"));
    if (mUser->isAvailable())
        all += mUser->listUnits(QStringLiteral(".service"));
    mModel->setUnits(all);
}

void ServicesTab::runOnSelection(Action action)
{
    const QModelIndexList rows = mView->selectionModel()->selectedRows();
    if (rows.isEmpty())
        return;

    QStringList failures;
    for (const QModelIndex &proxyIdx : rows) {
        const int srcRow = mProxy->mapToSource(proxyIdx).row();
        const UnitInfo &u = mModel->unitAt(srcRow);
        SystemdManager *mgr = u.scope == QLatin1String("System") ? mSystem : mUser;

        QString error;
        bool ok = false;
        switch (action) {
        case Start:   ok = mgr->startUnit(u.id, error); break;
        case Stop:    ok = mgr->stopUnit(u.id, error); break;
        case Restart: ok = mgr->restartUnit(u.id, error); break;
        case Enable:  ok = mgr->enableUnit(u.id, error); break;
        case Disable: ok = mgr->disableUnit(u.id, error); break;
        }
        if (!ok)
            failures << QStringLiteral("%1: %2").arg(u.id, error);
    }

    if (!failures.isEmpty())
        QMessageBox::warning(this, i18nc("@title:window", "Action Failed"),
                             failures.join(QLatin1Char('\n')));
    refresh();
}
