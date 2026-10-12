/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CoreIpcClient.h"

#include "common/Constants.h"
#include "common/TransferProtocol.h"

#include <QDebug>
#include <QLocalSocket>
#include <QObject>
#include <QString>

namespace deskflow::gui::ipc {

CoreIpcClient::CoreIpcClient(QObject *parent) : IpcClient(parent, kCoreIpcName, QStringLiteral("core"))
{
  // do nothing
}

void CoreIpcClient::sendStop()
{
  sendMessage(QStringLiteral("stop"));
}

bool CoreIpcClient::sendStartDrag(const QByteArray &payload, const QString &targetScreen)
{
  const auto wire = deskflow::transfer::encode({}, targetScreen, payload);
  return !wire.isEmpty() && sendMessage(QStringLiteral("startDrag=%1").arg(QString::fromLatin1(wire.toBase64())));
}

void CoreIpcClient::sendClearDrag()
{
  sendMessage(QStringLiteral("clearDrag"));
}

bool CoreIpcClient::sendFileChunk(uint8_t mark, const QByteArray &chunk, const QString &targetScreen)
{
  const auto wire = deskflow::transfer::encode({}, targetScreen, chunk);
  return !wire.isEmpty() &&
         sendMessage(QStringLiteral("sendFileChunk=%1:%2").arg(mark).arg(QString::fromLatin1(wire.toBase64())));
}

void CoreIpcClient::processCommand(const QString &command, const QStringList &parts)
{
  const auto args = parts.size() >= 2 ? parts.mid(1).join('=') : QString();
  Q_EMIT commandReceived(command, args);
}

} // namespace deskflow::gui::ipc
