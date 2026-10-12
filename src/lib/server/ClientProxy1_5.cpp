/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2013 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/ClientProxy1_5.h"

#include "base/Log.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"
#include "deskflow/StreamChunker.h"
#include "deskflow/ipc/CoreIpc.h"
#include "io/IStream.h"
#include "server/Server.h"

#include <QByteArray>
#include <QString>
#include <cstring>

//
// ClientProxy1_5
//

ClientProxy1_5::ClientProxy1_5(const std::string &name, deskflow::IStream *stream, Server *server, IEventQueue *events)
    : ClientProxy1_4(name, stream, server, events)
{
  // do nothing
}

void ClientProxy1_5::sendDragInfo(uint32_t fileCount, const char *info, size_t size)
{
  std::string data(info, size);
  LOG_DEBUG("sending drag info to client \"%s\": count=%u, size=%zu", getName().c_str(), fileCount, size);
  ProtocolUtil::writef(getStream(), kMsgDDragInfo, fileCount, &data);
}

void ClientProxy1_5::fileChunkSending(uint8_t mark, char *data, size_t dataSize)
{
  std::string chunk(data, dataSize);
  LOG_VERBOSE("sending file chunk to client \"%s\": mark=%u, size=%zu", getName().c_str(), mark, dataSize);
  ProtocolUtil::writef(getStream(), kMsgDFileTransfer, mark, &chunk);
}

bool ClientProxy1_5::parseMessage(const uint8_t *code)
{
  if (memcmp(code, kMsgDFileTransfer, 4) == 0) {
    fileChunkReceived();
  } else if (memcmp(code, kMsgDDragInfo, 4) == 0) {
    dragInfoReceived();
  } else {
    return ClientProxy1_4::parseMessage(code);
  }

  return true;
}

void ClientProxy1_5::fileChunkReceived() const
{
  uint8_t mark = 0;
  std::string content;
  ProtocolUtil::readf(getStream(), kMsgDFileTransfer + 4, &mark, &content);
  LOG_VERBOSE("received file chunk from client \"%s\": mark=%u, size=%zu", getName().c_str(), mark, content.size());
  const auto encoded = QString::fromLatin1(QByteArray(content.data(), static_cast<qsizetype>(content.size())).toBase64());
  ipcSendToClient(QStringLiteral("fileChunk"), QStringLiteral("%1:%2").arg(mark).arg(encoded));
}

void ClientProxy1_5::dragInfoReceived() const
{
  uint16_t fileCount = 0;
  std::string content;
  ProtocolUtil::readf(getStream(), kMsgDDragInfo + 4, &fileCount, &content);
  LOG_DEBUG("received drag info from client \"%s\": count=%u, size=%zu", getName().c_str(), fileCount, content.size());
  const auto encoded = QString::fromLatin1(QByteArray(content.data(), static_cast<qsizetype>(content.size())).toBase64());
  ipcSendToClient(QStringLiteral("dragInfo"), QStringLiteral("%1:%2").arg(fileCount).arg(encoded));
}
