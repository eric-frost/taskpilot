// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#include "servicemodel.h"

#include <KLocalizedString>

#include <QColor>

ServiceModel::ServiceModel(QObject *parent) : QAbstractTableModel(parent) {}

void ServiceModel::setUnits(const QVector<UnitInfo> &units)
{
    beginResetModel();
    mUnits = units;
    endResetModel();
}

int ServiceModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : mUnits.size();
}

int ServiceModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant ServiceModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= mUnits.size())
        return {};
    const UnitInfo &u = mUnits.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case Name: return u.id;
        case Status: return u.activeState.isEmpty()
                ? QString() : QStringLiteral("%1 (%2)").arg(u.activeState, u.subState);
        case Scope: return u.scope;
        case Startup: return u.unitFileState;
        case Description: return u.description;
        }
    } else if (role == Qt::ForegroundRole && index.column() == Status) {
        if (u.activeState == QLatin1String("active") || u.activeState == QLatin1String("activating"))
            return QColor(0x27, 0xae, 0x60);
        if (u.activeState == QLatin1String("failed"))
            return QColor(0xc0, 0x39, 0x2b);
        if (u.activeState == QLatin1String("inactive"))
            return QColor(0x7f, 0x8c, 0x8d);
    }
    return {};
}

QVariant ServiceModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return {};
    switch (section) {
    case Name: return i18nc("@title:column", "Service Name");
    case Status: return i18nc("@title:column", "Status");
    case Scope: return i18nc("@title:column", "Scope");
    case Startup: return i18nc("@title:column", "Startup Enabled");
    case Description: return i18nc("@title:column", "Description");
    }
    return {};
}
