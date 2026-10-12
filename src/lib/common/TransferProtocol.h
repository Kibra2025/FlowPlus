/*
 * SPDX-FileCopyrightText: (C) 2026 FlowPlus contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace deskflow::transfer {

inline constexpr qsizetype kMaxPayload = 128 * 1024;
inline constexpr qsizetype kMaxIpcMessage = 256 * 1024;
inline constexpr qsizetype kMaxRouteHeader = 1024;
inline constexpr int kDragLifetimeMs = 120000;

// FlowPlus extension inside the inherited DDRG/DFTR frames. The server supplies
// the source identity from the authenticated connection, never from file metadata.
struct RoutedPayload
{
  QString source;
  QString target;
  QByteArray data;
};

inline QByteArray encode(const QString &source, const QString &target, const QByteArray &data)
{
  if (source.toUtf8().size() > 255 || target.toUtf8().size() > 255 || data.size() > kMaxPayload ||
      source.contains(QChar::Null) || target.contains(QChar::Null)) {
    return {};
  }
  const auto header = QJsonDocument(
                          QJsonObject{
                              {QStringLiteral("flowplusRoute"), 1},
                              {QStringLiteral("source"), source},
                              {QStringLiteral("target"), target}
                          }
  ).toJson(QJsonDocument::Compact);
  if (header.size() > kMaxRouteHeader) {
    return {};
  }
  return header + '\n' + data;
}

inline bool decode(const QByteArray &wire, RoutedPayload &result)
{
  const auto separator = wire.indexOf('\n');
  if (separator <= 0 || separator > kMaxRouteHeader || wire.size() > kMaxPayload + kMaxRouteHeader + 1) {
    return false;
  }
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(wire.first(separator), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    return false;
  }
  const auto header = document.object();
  if (header.value(QStringLiteral("flowplusRoute")).toInt() != 1 ||
      !header.value(QStringLiteral("source")).isString() || !header.value(QStringLiteral("target")).isString()) {
    return false;
  }
  result = {
      header.value(QStringLiteral("source")).toString(), header.value(QStringLiteral("target")).toString(),
      wire.mid(separator + 1)
  };
  return result.source.toUtf8().size() <= 255 && result.target.toUtf8().size() <= 255 &&
         result.data.size() <= kMaxPayload && !result.source.contains(QChar::Null) &&
         !result.target.contains(QChar::Null);
}

struct NumberedPayload
{
  quint32 number = 0;
  QByteArray wire;
  RoutedPayload route;
};

inline bool decodeNumberedIpc(const QString &arguments, quint32 maximum, NumberedPayload &result)
{
  const auto separator = arguments.indexOf(':');
  if (separator <= 0 || arguments.size() > kMaxIpcMessage) {
    return false;
  }
  bool validNumber = false;
  result.number = arguments.first(separator).toUInt(&validNumber);
  if (!validNumber || result.number > maximum) {
    return false;
  }
  result.wire =
      QByteArray::fromBase64(arguments.mid(separator + 1).toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
  return decode(result.wire, result.route);
}

} // namespace deskflow::transfer
