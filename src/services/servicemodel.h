// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QAbstractTableModel>
#include <QVector>

#include "systemd/systemdmanager.h"

/** Table model over a flat list of systemd units (services). */
class ServiceModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { Name, Status, Scope, Startup, Description, ColumnCount };

    explicit ServiceModel(QObject *parent = nullptr);

    void setUnits(const QVector<UnitInfo> &units);
    const UnitInfo &unitAt(int row) const { return mUnits.at(row); }

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QVector<UnitInfo> mUnits;
};
