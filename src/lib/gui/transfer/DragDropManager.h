/*
 * FlowPlus -- mouse, keyboard, and file sharing utility
 * SPDX-FileCopyrightText: (C) 2026 FlowPlus Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "DragOverlayWidget.h"
#include "LocalSendService.h"

#include <QFile>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QStringList>
#include <QWidget>

namespace deskflow::gui {
class CoreProcess;
}

namespace deskflow::gui::transfer {

class DragDropManager;

class EdgeDropSensor : public QWidget
{
  Q_OBJECT

public:
  enum class Edge
  {
    Left,
    Right,
    Top,
    Bottom
  };

  explicit EdgeDropSensor(Edge edge, DragDropManager *manager, QWidget *parent = nullptr);
  ~EdgeDropSensor() override = default;

  void updateGeometryForScreen(const QRect &screenRect);

protected:
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dragMoveEvent(QDragMoveEvent *event) override;
  void dropEvent(QDropEvent *event) override;

private:
  Edge m_edge;
  DragDropManager *m_manager = nullptr;
};

class DragDropManager : public QObject
{
  Q_OBJECT

public:
  explicit DragDropManager(deskflow::gui::CoreProcess &coreProcess, QObject *parent = nullptr);
  ~DragDropManager() override;

  LocalSendService *localSendService() const
  {
    return m_localSend;
  }

  void setKnownPeerAddress(const QString &peerAddress);
  void stageFilesForDrag(const QStringList &filePaths, bool notifyPeerImmediately = true);
  void triggerImmediateSend(const QStringList &filePaths, const QPoint &dropPos = QPoint(-1, -1));
  void updateEdgeSensors();

Q_SIGNALS:
  void statusMessage(const QString &message);
  void transferProgressUpdated(const QString &fileName, int percent);
  void filesReceived(const QStringList &savedPaths);

private Q_SLOTS:
  void onScreenSwitched(const QString &screenName, bool isPrimary, int x, int y);
  void onClientScreenEntered(int x, int y);
  void onClientScreenLeft();
  void onMouseButtonUp(const QString &screenName, int x, int y);
  void onDragInfoReceived(uint32_t fileCount, const QByteArray &payload);
  void onFileChunkReceived(uint8_t mark, const QByteArray &chunk);
  void onOverlayDropDetected(const QPoint &screenPos);
  void onLocalSendTransferFailed(const QString &reason, const QStringList &originalFiles, const QPoint &dropPos);

private:
  void sendFilesViaTunnel(const QStringList &filePaths, const QPoint &dropPos);
  void handleCompletedFiles(const QStringList &savedPaths, const QPoint &dropPos);
  void injectNativeDrop(const QStringList &savedPaths, const QPoint &dropPos);
  QString resolveLocalIpAddress() const;
  static QString formatFileSize(qint64 bytes);

  deskflow::gui::CoreProcess &m_coreProcess;
  LocalSendService *m_localSend = nullptr;
  DragOverlayWidget *m_overlay = nullptr;
  QList<EdgeDropSensor *> m_edgeSensors;

  QStringList m_stagedFiles;
  QString m_knownPeerIp;
  QString m_incomingSenderIp;
  quint16 m_incomingSenderPort = kLocalSendDefaultPort;
  bool m_remoteDragActive = false;

  // Tunnel fallback receive state
  QFile *m_tunnelIncomingFile = nullptr;
  QString m_tunnelIncomingFileName;
  QString m_tunnelIncomingFilePath;
  qint64 m_tunnelIncomingTotalBytes = 0;
  qint64 m_tunnelIncomingReceivedBytes = 0;
  QPoint m_tunnelIncomingDropPos = QPoint(-1, -1);
};

} // namespace deskflow::gui::transfer
