/*
 * FlowPlus -- mouse, keyboard, and file sharing utility
 * SPDX-FileCopyrightText: (C) 2026 FlowPlus Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QDir>
#include <QFile>
#include <QStorageInfo>
#include <QTemporaryFile>

#include <cstdint>
#include <limits>

#if defined(Q_OS_UNIX)
#include <sys/statvfs.h>
#endif

namespace deskflow::gui::transfer {

// Query the destination path, not its reported volume root. On macOS APFS,
// writable data directories can share a reported root with the read-only system
// volume. A volume's read-only flag does not establish directory writability.
inline qint64 availableTransferBytes(const QString &directory)
{
#if defined(Q_OS_UNIX)
  struct statvfs status = {};
  const auto path = QFile::encodeName(directory);
  if (::statvfs(path.constData(), &status) != 0) {
    return -1;
  }
  const auto blocks = static_cast<std::uintmax_t>(status.f_bavail);
  const auto blockSize = static_cast<std::uintmax_t>(status.f_frsize ? status.f_frsize : status.f_bsize);
  if (blockSize == 0) {
    return -1;
  }
  constexpr auto maximum = std::numeric_limits<qint64>::max();
  return blocks > static_cast<std::uintmax_t>(maximum) / blockSize ? maximum : static_cast<qint64>(blocks * blockSize);
#else
  const QStorageInfo storage(directory);
  return storage.isValid() && storage.isReady() ? storage.bytesAvailable() : -1;
#endif
}

inline bool prepareTransferDirectory(const QString &directory, qint64 totalBytes)
{
  if (totalBytes < 0 || !QDir().mkpath(directory)) {
    return false;
  }
  // An actual create operation also catches directory permissions and a full
  // disk. The probe removes itself on every return path.
  QTemporaryFile probe(QDir(directory).filePath(QStringLiteral(".flowplus-check-XXXXXX.part")));
  if (!probe.open()) {
    return false;
  }
  const auto available = availableTransferBytes(directory);
  return available < 0 || totalBytes <= available;
}

} // namespace deskflow::gui::transfer
