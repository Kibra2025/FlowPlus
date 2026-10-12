/*
 * SPDX-FileCopyrightText: (C) 2026 FlowPlus contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/Event.h"
#include <QByteArray>

// EventQueue owns this object even if the destination is destroyed before dispatch.
class TransferEvent : public EventData
{
public:
  explicit TransferEvent(QByteArray payload, uint8_t chunkMark = 0) : data(std::move(payload)), mark(chunkMark)
  {
  }
  QByteArray data;
  uint8_t mark;
};
