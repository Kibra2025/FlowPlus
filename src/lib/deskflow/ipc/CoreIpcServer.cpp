/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CoreIpcServer.h"

#include "base/Log.h"
#include "common/Constants.h"
#include "common/Settings.h"
#include "common/TransferProtocol.h"

#include <QLocalSocket>

namespace deskflow::core::ipc {

static CoreIpcServer *s_instance = nullptr;

CoreIpcServer::CoreIpcServer(QObject *parent) : IpcServer(parent, kCoreIpcName, QStringLiteral("core"))
{
  assert(s_instance == nullptr);
  s_instance = this;
}

CoreIpcServer::~CoreIpcServer()
{
  if (s_instance == this) {
    s_instance = nullptr;
  }
}

bool CoreIpcServer::hasInstance()
{
  return s_instance != nullptr;
}

CoreIpcServer &CoreIpcServer::instance()
{
  assert(s_instance != nullptr);
  return *s_instance;
}

void CoreIpcServer::setStagedDragInfo(const QByteArray &dragPayload)
{
  QMutexLocker lock(&m_dragMutex);
  m_stagedDragPayload = dragPayload;
  m_dragAge.start();
}

QByteArray CoreIpcServer::stagedDragInfo() const
{
  QMutexLocker lock(&m_dragMutex);
  return m_dragAge.isValid() && m_dragAge.elapsed() < deskflow::transfer::kDragLifetimeMs ? m_stagedDragPayload
                                                                                          : QByteArray();
}

void CoreIpcServer::clearStagedDragInfo()
{
  QMutexLocker lock(&m_dragMutex);
  m_stagedDragPayload.clear();
  m_dragAge.invalidate();
}

void CoreIpcServer::processCommand(QLocalSocket *clientSocket, const QString &command, const QStringList &parts)
{
  const auto args = parts.size() >= 2 ? parts.mid(1).join('=') : QString();
  if (command == QStringLiteral("stop")) {
    LOG_DEBUG("core ipc server got stop message");
    writeToClientSocket(clientSocket, QStringLiteral("ok"));
    broadcastCommand(QStringLiteral("bye"));
    Q_EMIT stopProcessRequested();
    return;
  }
  if (command == QStringLiteral("startDrag")) {
    const auto raw = QByteArray::fromBase64(args.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    deskflow::transfer::RoutedPayload route;
    if (!Settings::value(Settings::Security::TlsEnabled).toBool() || !deskflow::transfer::decode(raw, route)) {
      writeToClientSocket(clientSocket, QStringLiteral("error=invalid transfer message"));
      return;
    }
    if (QJsonDocument::fromJson(route.data).object().value(QStringLiteral("action")).toString() == "dragStart") {
      setStagedDragInfo(raw);
    }
    writeToClientSocket(clientSocket, QStringLiteral("ok"));
    Q_EMIT startDragRequested(raw);
    return;
  }
  if (command == QStringLiteral("clearDrag")) {
    clearStagedDragInfo();
    writeToClientSocket(clientSocket, QStringLiteral("ok"));
    return;
  }
  if (command == QStringLiteral("sendFileChunk")) {
    deskflow::transfer::NumberedPayload payload;
    if (!Settings::value(Settings::Security::TlsEnabled).toBool() ||
        !deskflow::transfer::decodeNumberedIpc(args, 255, payload)) {
      writeToClientSocket(clientSocket, QStringLiteral("error=invalid transfer chunk"));
      return;
    }
    writeToClientSocket(clientSocket, QStringLiteral("ok"));
    Q_EMIT sendFileChunkRequested(static_cast<uint8_t>(payload.number), payload.wire);
    return;
  }
  LOG_WARN("core ipc server got unknown command: %s", command.toUtf8().constData());
}

} // namespace deskflow::core::ipc
