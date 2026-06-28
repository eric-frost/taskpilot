// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>

/** Coloured action icons, matching the look of the original TaskPilot (green
    play, red stop, …). resolve() returns a painted icon for the handful of
    lifecycle actions that benefit from colour, and falls back to the theme icon
    for everything else — so call sites can keep passing theme names. */
namespace Icons {

inline QPixmap canvas()
{
    QPixmap p(32, 32);
    p.fill(Qt::transparent);
    return p;
}

inline QIcon play() // green triangle
{
    QPixmap p = canvas();
    QPainter g(&p);
    g.setRenderHint(QPainter::Antialiasing);
    g.setPen(Qt::NoPen);
    g.setBrush(QColor(QStringLiteral("#27ae60")));
    g.drawPolygon(QPolygonF({{9, 5}, {26, 16}, {9, 27}}));
    return QIcon(p);
}

inline QIcon stop() // red square
{
    QPixmap p = canvas();
    QPainter g(&p);
    g.setRenderHint(QPainter::Antialiasing);
    g.setPen(Qt::NoPen);
    g.setBrush(QColor(QStringLiteral("#c0392b")));
    g.drawRoundedRect(8, 8, 16, 16, 2, 2);
    return QIcon(p);
}

inline QIcon restart() // blue circular arrow
{
    QPixmap p = canvas();
    QPainter g(&p);
    g.setRenderHint(QPainter::Antialiasing);
    g.setPen(QPen(QColor(QStringLiteral("#2980b9")), 3.5));
    g.drawArc(QRectF(7, 7, 18, 18), 70 * 16, 250 * 16);
    g.setPen(Qt::NoPen);
    g.setBrush(QColor(QStringLiteral("#2980b9")));
    g.drawPolygon(QPolygonF({{20, 3}, {27, 9}, {18, 11}})); // arrowhead at arc start
    return QIcon(p);
}

inline QIcon check() // green check
{
    QPixmap p = canvas();
    QPainter g(&p);
    g.setRenderHint(QPainter::Antialiasing);
    g.setPen(QPen(QColor(QStringLiteral("#27ae60")), 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    g.drawPolyline(QPolygonF({{7, 17}, {13, 24}, {26, 8}}));
    return QIcon(p);
}

inline QIcon ban() // red circle-slash
{
    QPixmap p = canvas();
    QPainter g(&p);
    g.setRenderHint(QPainter::Antialiasing);
    g.setPen(QPen(QColor(QStringLiteral("#c0392b")), 3.5));
    g.drawEllipse(QRectF(6, 6, 20, 20));
    g.drawLine(QPointF(11, 11), QPointF(21, 21));
    return QIcon(p);
}

/** Coloured icon for a known theme name, else the theme icon itself. */
inline QIcon resolve(const QString &themeName)
{
    if (themeName == QLatin1String("media-playback-start")) return play();
    if (themeName == QLatin1String("media-playback-stop")) return stop();
    if (themeName == QLatin1String("dialog-ok-apply")) return check();
    if (themeName == QLatin1String("dialog-cancel")) return ban();
    return QIcon::fromTheme(themeName);
}

} // namespace Icons
