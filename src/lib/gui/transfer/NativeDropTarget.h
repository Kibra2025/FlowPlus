/*
 * FlowPlus -- mouse, keyboard, and file sharing utility
 * SPDX-FileCopyrightText: (C) 2026 FlowPlus Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QString>
#include <QStringList>

#include <functional>

namespace deskflow::gui::transfer {

struct NativeDropTarget
{
  // A canonical directory captured at release. Never resolve the foreground
  // window again after a download: it may have navigated elsewhere by then.
  QString directory;
};

struct NativeDropPlacement
{
  QStringList paths;
  bool usedDropTarget = false;
  // Downloads preserved after a failed placement, interrupted cleanup, or a
  // local edit that makes deleting the original received copy unsafe.
  QStringList remainingDownloadPaths;
};

// Call on the GUI thread immediately when the remote mouse button is released.
// Unsupported windows, virtual folders and denied permissions return no target.
NativeDropTarget captureNativeDropTarget();

// Places complete received downloads in a previously captured directory.
// Existing files are never replaced. On failure download paths remain intact
// and copies made by this batch are rolled back. After the whole batch commits,
// unchanged download copies are removed; sender-side originals are never inputs
// to this function. Run on a worker thread because copying can require disk I/O.
NativeDropPlacement placeReceivedFiles(
    const QStringList &savedPaths, const NativeDropTarget &target, const std::function<bool()> &cancelled = {}
);

} // namespace deskflow::gui::transfer
