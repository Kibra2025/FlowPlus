/*
 * FlowPlus -- mouse, keyboard, and file sharing utility
 * SPDX-FileCopyrightText: (C) 2026 FlowPlus Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QtGlobal>

namespace deskflow::gui::transfer {

// The native drag pasteboard retains the previous drag after its release.
// Observe every revision, including while our screen is inactive, so a later
// ordinary click or returning pointer cannot send those old files again.
class DragSourceState
{
public:
  bool observe(qint64 revision, bool leftButtonDown, bool localScreenActive)
  {
    const bool changed = m_initialized && revision != m_revision;
    m_revision = revision;
    m_initialized = true;
    return changed && leftButtonDown && localScreenActive;
  }

  void reset()
  {
    m_initialized = false;
  }

private:
  qint64 m_revision = 0;
  bool m_initialized = false;
};

} // namespace deskflow::gui::transfer
