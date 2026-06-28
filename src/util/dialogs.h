// SPDX-FileCopyrightText: 2026 Eric Frost
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

class QWidget;

namespace Dialogs {

/** Show @p text in a non-modal, read-only monospace viewer (container logs,
    inspect output, bind-link status, …). Scrolls to the bottom for log-like
    output. The dialog deletes itself on close. */
void showText(QWidget *parent, const QString &title, const QString &text,
              bool toBottom = false, int w = 820, int h = 520);

} // namespace Dialogs
