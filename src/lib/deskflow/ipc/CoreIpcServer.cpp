/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CoreIpcServer.h"

#include "base/Log.h"
#include "common/Constants.h"

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
  m_stagedDragPayload = dragPayload;
}

QByteArray CoreIpcServer::stagedDragInfo() const
{
  return m_stagedDragPayload;
}

void CoreIpcServer::clearStagedDragInfo()
{
  m_stagedDragPayload.clear();
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
    LOG_DEBUG("core ipc server got startDrag message");
    m_stagedDragPayload = QByteArray::fromBase64(args.toUtf8());
    writeToClientSocket(clientSocket, QStringLiteral("ok"));
    Q_EMIT startDragRequested(args);
    return;
  }
  if (command == QStringLiteral("clearDrag")) {
    m_stagedDragPayload.clear();
    writeToClientSocket(clientSocket, QStringLiteral("ok"));
    return;
  }
  if (command == QStringLiteral("sendFileChunk")) {
    writeToClientSocket(clientSocket, QStringLiteral("ok"));
    Q_EMIT sendFileChunkRequested(args);
    return;
  }
  if (command == QStringLiteral("requestDrop")) {
    writeToClientSocket(clientSocket, QStringLiteral("ok"));
    Q_EMIT dropRequested(args);
    return;
  }
  LOG_WARN("core ipc server got unknown command: %s", command.toUtf8().constData());
}

} // namespace deskflow::core::ipc
