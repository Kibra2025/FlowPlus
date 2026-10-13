/*
 * FlowPlus -- mouse, keyboard, and file sharing utility
 * SPDX-FileCopyrightText: (C) 2026 FlowPlus Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "DragSourceState.h"

#include <QStringList>

namespace deskflow::gui::transfer {

// Call on the GUI thread. Uses the drag pasteboard, never the general clipboard.
QStringList pollNativeDragSource(DragSourceState &state, bool localScreenActive);

} // namespace deskflow::gui::transfer
