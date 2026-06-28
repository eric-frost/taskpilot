// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rowtablemodel.h"

#include <QColor>

RowTableModel::RowTableModel(QStringList headers, int statusColumn, QObject *parent)
    : QAbstractTableModel(parent), mHeaders(std::move(headers)), mStatusColumn(statusColumn)
{
}

void RowTableModel::setRows(const QVector<QStringList> &rows)
{
    beginResetModel();
    mRows = rows;
    endResetModel();
}

int RowTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : mRows.size();
}

int RowTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : mHeaders.size();
}

QVariant RowTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= mRows.size())
        return {};
    const QStringList &r = mRows.at(index.row());

    if (role == Qt::DisplayRole || role == Qt::ToolTipRole)
        return index.column() < r.size() ? r.at(index.column()) : QString();

    if (role == Qt::ForegroundRole && index.column() == mStatusColumn && mStatusColumn < r.size()) {
        const QString s = r.at(mStatusColumn).toLower();
        if (s.contains(QLatin1String("fail")) || s.contains(QLatin1String("error")))
            return QColor(0xc0, 0x39, 0x2b);
        if (s.contains(QLatin1String("active")) || s.contains(QLatin1String("running"))
            || s.startsWith(QLatin1String("up")) || s.contains(QLatin1String("mounted")))
            return QColor(0x27, 0xae, 0x60);
        if (s.contains(QLatin1String("inactive")) || s.contains(QLatin1String("dead"))
            || s.contains(QLatin1String("exited")) || s.contains(QLatin1String("stopped")))
            return QColor(0x7f, 0x8c, 0x8d);
    }
    return {};
}

QVariant RowTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal && section < mHeaders.size())
        return mHeaders.at(section);
    return {};
}
