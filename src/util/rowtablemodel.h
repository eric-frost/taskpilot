// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QAbstractTableModel>
#include <QStringList>
#include <QVector>

/** A generic table model over a list of string rows. One @p statusColumn may be
    colour-coded by keyword (active/running green, failed/error red, inactive/dead
    grey) — covering every TaskPilot table tab without a bespoke model each. */
class RowTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit RowTableModel(QStringList headers, int statusColumn, QObject *parent = nullptr);

    void setRows(const QVector<QStringList> &rows);
    QStringList row(int r) const { return r >= 0 && r < mRows.size() ? mRows.at(r) : QStringList(); }

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QStringList mHeaders;
    int mStatusColumn;
    QVector<QStringList> mRows;
};
