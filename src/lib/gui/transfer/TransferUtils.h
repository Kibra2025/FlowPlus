/*
 * FlowPlus -- mouse, keyboard, and file sharing utility
 * SPDX-FileCopyrightText: (C) 2026 FlowPlus Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QUuid>

#include <cmath>

namespace deskflow::gui::transfer {
constexpr int kMaxTransferFiles = 128;
constexpr qint64 kMaxTransferBytes = 64LL * 1024 * 1024 * 1024;
constexpr int kMaxFileNameLength = 255;

inline bool isPortableFileName(const QString &name)
{
  if (name.isEmpty() || name.size() > kMaxFileNameLength || name.toUtf8().size() > kMaxFileNameLength ||
      name == QStringLiteral(".") || name == QStringLiteral("..") || name.contains('/') || name.contains('\\')) {
    return false;
  }
  for (const auto character : name) {
    if (character.unicode() < 32 || character.unicode() == 127 || QStringLiteral(":*?\"<>|").contains(character)) {
      return false;
    }
  }
  const auto stem = name.section('.', 0, 0).trimmed().toUpper();
  const bool reservedPort = stem.size() == 4 && (stem.startsWith("COM") || stem.startsWith("LPT")) &&
                            ((stem.at(3) >= QChar('1') && stem.at(3) <= QChar('9')) ||
                             QStringLiteral("\u00b9\u00b2\u00b3").contains(stem.at(3)));
  return !name.endsWith('.') && !name.endsWith(' ') && stem != QStringLiteral("CON") && stem != QStringLiteral("PRN") &&
         stem != QStringLiteral("AUX") && stem != QStringLiteral("NUL") && !reservedPort;
}

inline bool isValidTransferId(const QString &id)
{
  return id.size() == 36 && !QUuid(id).isNull() && QUuid(id).toString(QUuid::WithoutBraces) == id;
}

inline int transferProgressPercent(qint64 transferred, qint64 total)
{
  return total > 0 ? static_cast<int>(qBound(0.0, 100.0 * transferred / total, 100.0)) : 100;
}
inline bool validateTransferManifest(const QJsonArray &manifest, qint64 &totalBytes)
{
  totalBytes = 0;
  if (manifest.isEmpty() || manifest.size() > kMaxTransferFiles) {
    return false;
  }
  for (const auto &entry : manifest) {
    if (!entry.isObject()) {
      return false;
    }
    const auto object = entry.toObject();
    const auto sizeValue = object.value(QStringLiteral("size"));
    const auto size = sizeValue.toDouble(-1);
    if (!isPortableFileName(object.value(QStringLiteral("fileName")).toString()) || !sizeValue.isDouble() ||
        !std::isfinite(size) || size < 0 || std::floor(size) != size || size > kMaxTransferBytes - totalBytes) {
      return false;
    }
    totalBytes += static_cast<qint64>(size);
  }
  return true;
}

inline QString uniqueTransferDestination(const QString &directory, const QString &fileName)
{
  if (!isPortableFileName(fileName)) {
    return {};
  }
  const QFileInfo info(fileName);
  for (int counter = 0; counter < 10000; ++counter) {
    auto candidateName = fileName;
    if (counter > 0) {
      auto base = info.suffix().isEmpty() ? fileName : info.completeBaseName();
      auto suffix = info.suffix().isEmpty() ? QString() : QStringLiteral(".") + info.suffix();
      const auto counterText = QStringLiteral(" (%1)").arg(counter);
      if ((counterText + suffix).toUtf8().size() >= kMaxFileNameLength) {
        base = fileName;
        suffix.clear();
      }
      const auto tail = counterText + suffix;
      while (!base.isEmpty() &&
             ((base + tail).size() > kMaxFileNameLength || (base + tail).toUtf8().size() > kMaxFileNameLength)) {
        base.chop(1);
        if (!base.isEmpty() && base.back().isHighSurrogate()) {
          base.chop(1);
        }
      }
      candidateName = base + tail;
      if (!isPortableFileName(candidateName)) {
        return {};
      }
    }
    const auto candidate = QDir(directory).filePath(candidateName);
    const QFileInfo candidateInfo(candidate);
    // A dangling symlink is also occupied. Never follow or replace it.
    if (!candidateInfo.exists() && !candidateInfo.isSymLink()) {
      return candidate;
    }
  }
  return {};
}

} // namespace deskflow::gui::transfer
