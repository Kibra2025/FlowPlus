/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CoreIpcClient.h"

#include "common/Constants.h"

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

void CoreIpcClient::sendStartDrag(const QByteArray &payload)
{
  sendMessage(QStringLiteral("startDrag"), QString::fromLatin1(payload.toBase64()));
}

void CoreIpcClient::sendClearDrag()
{
  sendMessage(QStringLiteral("clearDrag"));
}

void CoreIpcClient::sendFileChunk(uint8_t mark, const QByteArray &chunk)
{
  const auto encoded = QStringLiteral("%1:%2").arg(mark).arg(QString::fromLatin1(chunk.toBase64()));
  sendMessage(QStringLiteral("sendFileChunk"), encoded);
}

void CoreIpcClient::sendRequestDrop(int x, int y)
{
  sendMessage(QStringLiteral("requestDrop"), QStringLiteral("%1,%2").arg(x).arg(y));
}

void CoreIpcClient::processCommand(const QString &command, const QStringList &parts)
{
  const auto args = parts.size() >= 2 ? parts.mid(1).join('=') : QString();
  Q_EMIT commandReceived(command, args);
}

} // namespace deskflow::gui::ipc
