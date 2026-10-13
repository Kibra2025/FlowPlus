/*
 * FlowPlus -- mouse, keyboard, and file sharing utility
 * SPDX-FileCopyrightText: (C) 2026 FlowPlus Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "NativeDropTarget.h"
#include "TransferUtils.h"

#include <QDateTime>
#include <QFile>
#include <QTemporaryFile>

namespace deskflow::gui::transfer {
namespace {

struct ReceivedCopy
{
  QString path;
  QString canonicalPath;
  qint64 size;
  QDateTime modified;
  QDateTime created;

  bool unchanged() const
  {
    const QFileInfo current(path);
    return current.isFile() && !current.isSymLink() && current.canonicalFilePath() == canonicalPath &&
           current.size() == size && current.lastModified() == modified &&
           (!created.isValid() || current.birthTime() == created);
  }
};

} // namespace

NativeDropPlacement placeReceivedFiles(
    const QStringList &savedPaths, const NativeDropTarget &target, const std::function<bool()> &cancelled
)
{
  const NativeDropPlacement fallback{savedPaths, false, savedPaths};
  const QFileInfo directory(target.directory);
  if (savedPaths.isEmpty() || target.directory.isEmpty() || !directory.isDir() ||
      directory.canonicalFilePath() != target.directory) {
    return fallback;
  }

  QStringList published;
  QStringList result;
  QList<ReceivedCopy> downloads;
  const auto rollback = [&] {
    for (const auto &path : published) {
      QFile::remove(path);
    }
    return fallback;
  };

  for (const auto &source : savedPaths) {
    if (cancelled && cancelled()) {
      return rollback();
    }
    const QFileInfo info(source);
    if (!info.isFile() || info.isSymLink() || !isPortableFileName(info.fileName())) {
      return rollback();
    }
    if (info.canonicalPath() == target.directory) {
      result.append(info.canonicalFilePath());
      continue;
    }
    QFile input(source);
    QTemporaryFile staged(QDir(target.directory).filePath(QStringLiteral(".flowplus-drop-XXXXXX.part")));
    if (!input.open(QIODevice::ReadOnly) || !staged.open()) {
      return rollback();
    }
    const auto expectedSize = input.size();
    const ReceivedCopy download{source, info.canonicalFilePath(), expectedSize, info.lastModified(), info.birthTime()};
    qint64 copied = 0;
    while (!input.atEnd()) {
      if (cancelled && cancelled()) {
        return rollback();
      }
      const auto data = input.read(1024 * 1024);
      if (data.isEmpty() || data.size() > expectedSize - copied || staged.write(data) != data.size()) {
        return rollback();
      }
      copied += data.size();
    }
    if ((cancelled && cancelled()) || input.error() != QFileDevice::NoError || copied != expectedSize ||
        !download.unchanged() || !staged.flush()) {
      return rollback();
    }
    staged.close();
    const auto destination = uniqueTransferDestination(target.directory, info.fileName());
    // QFile::rename refuses to replace existing files, including a destination
    // created after the unique-name check. Keep the download on any such race.
    if (destination.isEmpty() || !staged.rename(destination)) {
      return rollback();
    }
    staged.setAutoRemove(false);
    published.append(destination);
    result.append(destination);
    downloads.append(download);
  }
  if (cancelled && cancelled()) {
    return rollback();
  }
  // Commit barrier: from here on destinations must never be rolled back. A
  // cancellation during cleanup preserves the remaining downloads instead.
  NativeDropPlacement placement{result, true, {}};
  bool interrupted = false;
  for (const auto &download : downloads) {
    interrupted = interrupted || (cancelled && cancelled());
    if (interrupted || !download.unchanged() || !QFile::remove(download.path)) {
      placement.remainingDownloadPaths.append(download.path);
    }
  }
  return placement;
}

#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
NativeDropTarget captureNativeDropTarget()
{
  return {};
}
#endif

} // namespace deskflow::gui::transfer
