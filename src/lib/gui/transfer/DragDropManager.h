/*
 * FlowPlus -- mouse, keyboard, and file sharing utility
 * SPDX-FileCopyrightText: (C) 2026 FlowPlus Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "DragOverlayWidget.h"
#include "LocalSendService.h"

#include <QFile>
#include <QJsonArray>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QTemporaryFile>
#include <QTimer>
#include <QWidget>

#include <memory>
#include <vector>

class QMessageBox;

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
  explicit EdgeDropSensor(Edge edge, DragDropManager *manager);
  void updateGeometryForScreen(const QRect &screenRect);

protected:
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dragMoveEvent(QDragMoveEvent *event) override;
  void dropEvent(QDropEvent *event) override;
  void paintEvent(QPaintEvent *event) override;

private:
  Edge m_edge;
  DragDropManager *m_manager;
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
  void onDragCancelled(const QString &transferId);
  void onDragInfoReceived(const QString &sourceScreen, uint32_t fileCount, const QByteArray &payload);
  void onFileChunkReceived(const QString &sourceScreen, uint8_t mark, const QByteArray &chunk);
  void onOverlayDropDetected(const QPoint &screenPos);
  void onLocalSendTransferFailed(
      const QString &reason, const QStringList &originalFiles, const QPoint &dropPos, bool allowFallback,
      const QString &transferId
  );

private:
  enum class TunnelPhase
  {
    Idle,
    AwaitingReady,
    AwaitingChunk,
    AwaitingComplete
  };
  bool canTransfer() const;
  bool stageFiles(const QStringList &filePaths);
  bool sendDragMessage(QJsonObject message, const QString &targetScreen);
  void advertiseFiles(const QString &action);
  void showIncomingDrag();
  void acceptIncomingTransfer(bool nativeDrop, const QPoint &dropPos);
  void cancelOutgoing(const QString &reason = QString());
  void cancelIncoming(const QString &reason = QString(), bool notifyPeer = true);
  void finishOutgoing();
  void sendFilesViaTunnel(const QStringList &filePaths);
  void startNextTunnelFile();
  void sendNextTunnelChunk();
  bool acknowledgeTunnel(const QString &action);
  bool publishTunnelFiles(QStringList &savedPaths);
  void handleCompletedFiles(const QStringList &savedPaths, const QPoint &dropPos, const QString &transferId);
  void captureNativeDropTarget();
  bool injectNativeDrop(const QStringList &savedPaths);
  QString resolveLocalIpAddress(const QString &peerAddress = QString()) const;
  static QString formatFileSize(qint64 bytes);

  deskflow::gui::CoreProcess &m_coreProcess;
  LocalSendService *m_localSend;
  DragOverlayWidget *m_overlay;
  QList<EdgeDropSensor *> m_edgeSensors;
  QStringList m_connectedScreens;
  QString m_lastRemoteScreen;
  QString m_knownPeerIp;
  bool m_localScreenActive = true;
  QTimer m_outgoingTimeout;
  QTimer m_incomingTimeout;
  QPointer<QMessageBox> m_offerDialog;

  // Replies are bound to a fresh UUID and the authenticated core screen.
  QString m_outgoingId;
  QString m_outgoingTargetScreen;
  QString m_outgoingAdvertisedIp;
  QStringList m_stagedFiles;
  QJsonArray m_outgoingManifest;
  bool m_outgoingSending = false;
  bool m_outgoingIsDrag = true;
  QStringList m_tunnelOutgoingPaths;
  std::unique_ptr<QFile> m_tunnelOutgoingFile;
  int m_tunnelOutgoingIndex = 0;
  qint64 m_tunnelOutgoingOffset = 0;
  TunnelPhase m_tunnelPhase = TunnelPhase::Idle;

  QString m_incomingId;
  QString m_incomingSourceScreen;
  QString m_incomingSenderIp;
  QJsonArray m_incomingManifest;
  bool m_remoteDragActive = false;
  bool m_incomingAccepted = false;
  bool m_incomingNativeDrop = false;
  QPoint m_incomingDropPos;
  std::unique_ptr<QTemporaryFile> m_tunnelIncomingFile;
  std::vector<std::unique_ptr<QTemporaryFile>> m_tunnelIncomingFiles;
  int m_tunnelIncomingIndex = 0;
  qint64 m_tunnelIncomingReceivedBytes = 0;

  // Capture the real window at release, before a potentially long download.
  quintptr m_nativeDropTarget = 0;
  quint32 m_nativeDropProcess = 0;
  QString m_nativeDropClass;
  QPoint m_nativeDropPoint;
};

} // namespace deskflow::gui::transfer
