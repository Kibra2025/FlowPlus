/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "IpcServer.h"

#include <QElapsedTimer>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <functional>

class QLocalSocket;

namespace deskflow::core::ipc {

class CoreIpcServer : public IpcServer
{
  Q_OBJECT

public:
  explicit CoreIpcServer(QObject *parent);
  ~CoreIpcServer() override;

  static bool hasInstance();
  static CoreIpcServer &instance();

  void setStagedDragInfo(const QByteArray &dragPayload);
  QByteArray stagedDragInfo() const;
  void clearStagedDragInfo();

Q_SIGNALS:
  void startDragRequested(const QByteArray &payload);
  void sendFileChunkRequested(uint8_t mark, const QByteArray &payload);

private:
  void processCommand(QLocalSocket *clientSocket, const QString &command, const QStringList &parts) override;

  QByteArray m_stagedDragPayload;
  mutable QMutex m_dragMutex;
  QElapsedTimer m_dragAge;
};

} // namespace deskflow::core::ipc
