/*
 * FlowPlus -- mouse, keyboard, and file sharing utility
 * SPDX-FileCopyrightText: (C) 2026 FlowPlus Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "DragDropManager.h"
#include "TransferStorage.h"
#include "TransferUtils.h"

#include "common/Settings.h"
#include "common/TransferProtocol.h"
#include "deskflow/ProtocolTypes.h"
#include "gui/core/CoreProcess.h"
#include "gui/core/NetworkMonitor.h"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QScreen>
#include <QStandardPaths>
#include <QUdpSocket>
#include <QUrl>
#include <QUuid>
#include <QtEndian>

#include <cstring>

#if defined(Q_OS_WIN)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <ShlObj.h>
#include <Windows.h>
#include <shellapi.h>
#endif

namespace deskflow::gui::transfer {
namespace {
constexpr qint64 kChunkSize = 32 * 1024;
constexpr int kUuidLength = 36;
constexpr int kChunkHeaderSize = kUuidLength + sizeof(quint64);
constexpr int kTransferTimeout = 30000;
constexpr int kDragTimeout = deskflow::transfer::kDragLifetimeMs;

QStringList localFilesFromMime(const QMimeData *mime)
{
  QStringList files;
  if (mime) {
    for (const auto &url : mime->urls()) {
      if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile() && !files.contains(url.toLocalFile())) {
        files.append(url.toLocalFile());
      }
    }
  }
  return files;
}

} // namespace

EdgeDropSensor::EdgeDropSensor(Edge edge, DragDropManager *manager)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus),
      m_edge(edge),
      m_manager(manager)
{
  setAcceptDrops(true);
  setAttribute(Qt::WA_ShowWithoutActivating, true);
  setAttribute(Qt::WA_TranslucentBackground, true);
}

void EdgeDropSensor::updateGeometryForScreen(const QRect &screenRect)
{
  constexpr int thickness = 4;
  switch (m_edge) {
  case Edge::Left:
    setGeometry(screenRect.left(), screenRect.top(), thickness, screenRect.height());
    break;
  case Edge::Right:
    setGeometry(screenRect.right() - thickness + 1, screenRect.top(), thickness, screenRect.height());
    break;
  case Edge::Top:
    setGeometry(screenRect.left(), screenRect.top(), screenRect.width(), thickness);
    break;
  case Edge::Bottom:
    setGeometry(screenRect.left(), screenRect.bottom() - thickness + 1, screenRect.width(), thickness);
    break;
  }
}

void EdgeDropSensor::paintEvent(QPaintEvent *)
{
  // Fully transparent Windows layered windows cannot receive native file drags.
  QPainter(this).fillRect(rect(), QColor(0, 0, 0, 2));
}

void EdgeDropSensor::dragEnterEvent(QDragEnterEvent *event)
{
  const auto files = localFilesFromMime(event->mimeData());
  if (!files.isEmpty()) {
    event->setDropAction(Qt::CopyAction);
    event->accept();
    m_manager->stageFilesForDrag(files);
  }
}

void EdgeDropSensor::dragMoveEvent(QDragMoveEvent *event)
{
  if (!localFilesFromMime(event->mimeData()).isEmpty()) {
    event->setDropAction(Qt::CopyAction);
    event->accept();
  }
}

void EdgeDropSensor::dropEvent(QDropEvent *event)
{
  // A local-edge release is an explicit send offer, not a remote native drop.
  const auto files = localFilesFromMime(event->mimeData());
  if (!files.isEmpty()) {
    event->setDropAction(Qt::CopyAction);
    event->accept();
    m_manager->triggerImmediateSend(files);
  }
}

DragDropManager::DragDropManager(deskflow::gui::CoreProcess &coreProcess, QObject *parent)
    : QObject(parent),
      m_coreProcess(coreProcess),
      m_localSend(new LocalSendService(this)),
      m_overlay(new DragOverlayWidget())
{
  m_localSend->setAlias(Settings::value(Settings::Core::ComputerName).toString());
  m_localSend->start(kLocalSendDefaultPort);
  m_outgoingTimeout.setSingleShot(true);
  m_incomingTimeout.setSingleShot(true);
  connect(&m_outgoingTimeout, &QTimer::timeout, this, [this] { cancelOutgoing(tr("File transfer timed out")); });
  connect(&m_incomingTimeout, &QTimer::timeout, this, [this] { cancelIncoming(tr("File transfer timed out")); });
  connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen *) { updateEdgeSensors(); });
  connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this](QScreen *) { updateEdgeSensors(); });
  connect(&m_coreProcess, &CoreProcess::processStateChanged, this, [this](auto state) {
    if (state != deskflow::core::ProcessState::Started) {
      cancelOutgoing();
      cancelIncoming({}, false);
    } else {
      m_localScreenActive = m_coreProcess.mode() == Settings::CoreMode::Server;
    }
    updateEdgeSensors();
  });
  connect(&m_coreProcess, &CoreProcess::connectionStateChanged, this, [this](auto state) {
    if (state == deskflow::core::ConnectionState::Disconnected) {
      cancelOutgoing();
      cancelIncoming({}, false);
      m_connectedScreens.clear();
      m_lastRemoteScreen.clear();
    }
    updateEdgeSensors();
  });
  connect(&m_coreProcess, &CoreProcess::fileTransferPeersChanged, this, [this](const QStringList &screens) {
    m_connectedScreens = screens;
    if (!m_lastRemoteScreen.isEmpty() && !screens.contains(m_lastRemoteScreen)) {
      m_lastRemoteScreen.clear();
    }
    if (!m_outgoingTargetScreen.isEmpty() && !screens.contains(m_outgoingTargetScreen) &&
        m_coreProcess.mode() == Settings::CoreMode::Server) {
      cancelOutgoing(tr("The destination disconnected"));
    }
    if (!m_incomingSourceScreen.isEmpty() && !screens.contains(m_incomingSourceScreen)) {
      cancelIncoming(tr("The sender disconnected"), false);
    }
    updateEdgeSensors();
  });
  connect(&m_coreProcess, &CoreProcess::screenSwitched, this, &DragDropManager::onScreenSwitched);
  connect(&m_coreProcess, &CoreProcess::clientScreenEntered, this, &DragDropManager::onClientScreenEntered);
  connect(&m_coreProcess, &CoreProcess::clientScreenLeft, this, &DragDropManager::onClientScreenLeft);
  connect(&m_coreProcess, &CoreProcess::mouseButtonUp, this, &DragDropManager::onMouseButtonUp);
  connect(&m_coreProcess, &CoreProcess::dragCancelled, this, &DragDropManager::onDragCancelled);
  connect(&m_coreProcess, &CoreProcess::dragInfoReceivedFrom, this, &DragDropManager::onDragInfoReceived);
  connect(&m_coreProcess, &CoreProcess::fileChunkReceivedFrom, this, &DragDropManager::onFileChunkReceived);
  connect(m_overlay, &DragOverlayWidget::dropDetected, this, &DragDropManager::onOverlayDropDetected);
  connect(
      m_localSend, &LocalSendService::transferProgress, this,
      [this](const QString &fileName, qint64 transferred, qint64 total) {
        const auto percent = transferProgressPercent(transferred, total);
        if (m_incomingAccepted) {
          m_incomingTimeout.start(kTransferTimeout);
          m_overlay->showTransferring(fileName, percent);
        }
        if (m_outgoingSending) {
          m_outgoingTimeout.start(kTransferTimeout);
        }
        Q_EMIT transferProgressUpdated(fileName, percent);
      }
  );
  connect(m_localSend, &LocalSendService::transferCompleted, this, &DragDropManager::handleCompletedFiles);
  connect(
      m_localSend, &LocalSendService::filesSent, this,
      [this](const QStringList &, const QPoint &, const QString &id) {
        if (id == m_outgoingId && m_outgoingSending) {
          finishOutgoing();
        }
      }
  );
  connect(m_localSend, &LocalSendService::transferFailed, this, &DragDropManager::onLocalSendTransferFailed);
  connect(
      m_localSend, &LocalSendService::incomingTransferFailed, this,
      [this](const QString &reason, const QString &id) {
        if (id == m_incomingId) {
          cancelIncoming(reason);
        }
      }
  );
  updateEdgeSensors();
}

DragDropManager::~DragDropManager()
{
  // MainWindow owns CoreProcess as a value member, but this manager as a QObject
  // child. The value member may already be gone during QObject child teardown.
  m_localSend->cancelOutgoingTransfer(m_outgoingId);
  m_outgoingId.clear();
  cancelOutgoing();
  cancelIncoming({}, false);
  qDeleteAll(m_edgeSensors);
  delete m_overlay;
}

bool DragDropManager::canTransfer() const
{
  if (!m_coreProcess.isStarted() || !Settings::value(Settings::Security::TlsEnabled).toBool()) {
    return false;
  }
  return m_coreProcess.mode() == Settings::CoreMode::Server
             ? !m_connectedScreens.isEmpty()
             : !m_connectedScreens.isEmpty() &&
                   m_coreProcess.connectionState() == deskflow::core::ConnectionState::Connected;
}

void DragDropManager::updateEdgeSensors()
{
  for (auto *sensor : m_edgeSensors) {
    sensor->hide();
    sensor->deleteLater();
  }
  m_edgeSensors.clear();
  if (!canTransfer()) {
    return;
  }
  for (auto *screen : QGuiApplication::screens()) {
    connect(screen, &QScreen::geometryChanged, this, &DragDropManager::updateEdgeSensors, Qt::UniqueConnection);
    for (const auto edge :
         {EdgeDropSensor::Edge::Left, EdgeDropSensor::Edge::Right, EdgeDropSensor::Edge::Top,
          EdgeDropSensor::Edge::Bottom}) {
      auto *sensor = new EdgeDropSensor(edge, this);
      sensor->updateGeometryForScreen(screen->geometry());
      sensor->show();
      m_edgeSensors.append(sensor);
    }
  }
}

void DragDropManager::setKnownPeerAddress(const QString &peerAddress)
{
  const auto input = peerAddress.trimmed();
  const QHostAddress address(input);
  m_knownPeerIp = address.isNull() ? QUrl(QStringLiteral("flowplus://") + input).host() : address.toString();
}

QString DragDropManager::resolveLocalIpAddress(const QString &peerAddress) const
{
  // Use the actual route when possible; the first adapter may not be the VPN.
  const QHostAddress peer(peerAddress);
  if (!peer.isNull()) {
    QUdpSocket routeProbe;
    routeProbe.connectToHost(peer, kLocalSendDefaultPort);
    const auto local = routeProbe.localAddress();
    if (!local.isNull() && local != QHostAddress::Any && local != QHostAddress::AnyIPv6) {
      return local.toString();
    }
  }
  const auto addresses = NetworkMonitor::validAddresses();
  return addresses.isEmpty() ? QString() : addresses.first();
}

QString DragDropManager::formatFileSize(qint64 bytes)
{
  if (bytes < 1024) {
    return QStringLiteral("%1 B").arg(bytes);
  }
  if (bytes < 1024 * 1024) {
    return QStringLiteral("%1 KiB").arg(QString::number(bytes / 1024.0, 'f', 1));
  }
  if (bytes < 1024LL * 1024 * 1024) {
    return QStringLiteral("%1 MiB").arg(QString::number(bytes / (1024.0 * 1024.0), 'f', 1));
  }
  return QStringLiteral("%1 GiB").arg(QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 1));
}

bool DragDropManager::stageFiles(const QStringList &filePaths)
{
  if (!canTransfer()) {
    Q_EMIT statusMessage(tr("Connect a FlowPlus peer with TLS enabled before sending files"));
    return false;
  }
  if (m_outgoingSending || !m_incomingId.isEmpty()) {
    Q_EMIT statusMessage(tr("Wait for the current transfer to finish"));
    return false;
  }
  QStringList files;
  QJsonArray manifest;
  for (const auto &path : filePaths) {
    const QFileInfo info(path);
    const auto canonical = info.canonicalFilePath();
    if (!info.isFile() || !info.isReadable() || canonical.isEmpty()) {
      cancelOutgoing(tr("Some selected files cannot be read"));
      return false;
    }
    if (!files.contains(canonical)) {
      files.append(canonical);
      manifest.append(
          QJsonObject{
              {QStringLiteral("fileName"), QFileInfo(canonical).fileName()}, {QStringLiteral("size"), info.size()}
          }
      );
    }
  }
  qint64 total = 0;
  if (!validateTransferManifest(manifest, total)) {
    cancelOutgoing(tr("Choose at most %1 files, totaling at most %2, with portable file names")
                       .arg(kMaxTransferFiles)
                       .arg(formatFileSize(kMaxTransferBytes)));
    return false;
  }
  if (m_stagedFiles == files && m_outgoingManifest == manifest && !m_outgoingId.isEmpty()) {
    m_outgoingTimeout.start(kDragTimeout);
    return true;
  }
  cancelOutgoing();
  m_stagedFiles = files;
  m_outgoingManifest = manifest;
  m_outgoingId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  m_outgoingIsDrag = true;
  m_outgoingTargetScreen = m_lastRemoteScreen;
  if (m_coreProcess.mode() == Settings::CoreMode::Server && m_outgoingTargetScreen.isEmpty() &&
      m_connectedScreens.size() == 1) {
    m_outgoingTargetScreen = m_connectedScreens.first();
  }
  m_outgoingTimeout.start(kDragTimeout);
  Q_EMIT statusMessage(tr("Files ready: %1 (%2)").arg(files.size()).arg(formatFileSize(total)));
  return true;
}

bool DragDropManager::sendDragMessage(QJsonObject message, const QString &targetScreen)
{
  return m_coreProcess.sendStartDrag(QJsonDocument(message).toJson(QJsonDocument::Compact), targetScreen);
}

void DragDropManager::advertiseFiles(const QString &action)
{
  if (m_outgoingId.isEmpty()) {
    return;
  }
  m_outgoingAdvertisedIp = resolveLocalIpAddress(m_knownPeerIp);
  const QJsonObject offer{
      {QStringLiteral("action"), action},
      {QStringLiteral("transferId"), m_outgoingId},
      {QStringLiteral("senderIp"), m_outgoingAdvertisedIp},
      {QStringLiteral("senderPort"), m_localSend->serverPort()},
      {QStringLiteral("files"), m_outgoingManifest}
  };
  if (!sendDragMessage(offer, m_outgoingTargetScreen)) {
    cancelOutgoing(tr("The destination is not available"));
  }
}

void DragDropManager::stageFilesForDrag(const QStringList &filePaths, bool notifyPeerImmediately)
{
  if (stageFiles(filePaths) && notifyPeerImmediately) {
    advertiseFiles(QStringLiteral("dragStart"));
  }
}

void DragDropManager::triggerImmediateSend(const QStringList &filePaths, const QPoint &dropPos)
{
  Q_UNUSED(dropPos)
  if (!stageFiles(filePaths)) {
    return;
  }
  if (m_coreProcess.mode() == Settings::CoreMode::Server && m_outgoingTargetScreen.isEmpty()) {
    cancelOutgoing(tr("Move the pointer onto the destination screen before choosing Send files"));
    return;
  }
  m_outgoingIsDrag = false;
  advertiseFiles(QStringLiteral("transferOffer"));
  if (!m_outgoingId.isEmpty()) {
    Q_EMIT statusMessage(tr("Waiting for the destination to accept the files"));
  }
}

void DragDropManager::onScreenSwitched(const QString &screenName, bool isPrimary, int x, int y)
{
  Q_UNUSED(x)
  Q_UNUSED(y)
  m_localScreenActive = isPrimary;
  if (!isPrimary) {
    m_lastRemoteScreen = screenName;
    if (!m_outgoingId.isEmpty() && !m_outgoingSending && m_outgoingIsDrag) {
      if (!m_outgoingTargetScreen.isEmpty() && m_outgoingTargetScreen != screenName) {
        sendDragMessage(
            {{QStringLiteral("action"), QStringLiteral("dragCancel")}, {QStringLiteral("transferId"), m_outgoingId}},
            m_outgoingTargetScreen
        );
      }
      m_outgoingTargetScreen = screenName;
      advertiseFiles(QStringLiteral("dragStart"));
    }
    if (m_remoteDragActive) {
      cancelIncoming({}, false);
    }
  } else {
    showIncomingDrag();
  }
}

void DragDropManager::onClientScreenEntered(int, int)
{
  m_localScreenActive = true;
  showIncomingDrag();
}

void DragDropManager::onClientScreenLeft()
{
  m_localScreenActive = false;
  if (m_remoteDragActive) {
    // Leaving a hovered receiver continues the same drag on another screen.
    // Only an actual rejection/cancel should abort the sender's staged files.
    cancelIncoming({}, false);
  }
}

void DragDropManager::onMouseButtonUp(const QString &, int, int)
{
  if (m_localScreenActive && m_remoteDragActive && !m_incomingAccepted) {
    onOverlayDropDetected(QCursor::pos());
  } else if (m_localScreenActive && !m_outgoingId.isEmpty() && !m_outgoingSending && m_outgoingIsDrag) {
    cancelOutgoing();
  }
}

void DragDropManager::onDragCancelled(const QString &transferId)
{
  if (transferId == m_outgoingId && m_outgoingIsDrag && !m_outgoingSending) {
    cancelOutgoing(tr("File drag canceled"));
  }
  if (transferId == m_incomingId && m_remoteDragActive && !m_incomingAccepted) {
    cancelIncoming(tr("File drag canceled"), false);
  }
}

void DragDropManager::showIncomingDrag()
{
  if (!m_remoteDragActive || !m_localScreenActive || m_incomingAccepted) {
    return;
  }
  qint64 total = 0;
  if (!validateTransferManifest(m_incomingManifest, total)) {
    cancelIncoming();
    return;
  }
  const auto summary = m_incomingManifest.size() == 1
                           ? tr("%1 (%2)").arg(
                                 m_incomingManifest.first().toObject().value(QStringLiteral("fileName")).toString(),
                                 formatFileSize(total)
                             )
                           : tr("%1 files (%2)").arg(m_incomingManifest.size()).arg(formatFileSize(total));
  m_overlay->showDragging(summary, tr("Release the left mouse button to receive these files"));
}

void DragDropManager::onDragInfoReceived(const QString &sourceScreen, uint32_t, const QByteArray &payload)
{
  if (sourceScreen.isEmpty() || payload.size() > 128 * 1024 || !canTransfer()) {
    return;
  }
  const auto doc = QJsonDocument::fromJson(payload);
  if (!doc.isObject()) {
    return;
  }
  const auto message = doc.object();
  const auto action = message.value(QStringLiteral("action")).toString();
  const auto id = message.value(QStringLiteral("transferId")).toString();
  if (!isValidTransferId(id)) {
    return;
  }
  // Only the authenticated primary can report cancellation of its shared input
  // gesture to a secondary source, regardless of that source's current target.
  if (action == QStringLiteral("dragCancelled") && m_coreProcess.mode() == Settings::CoreMode::Client &&
      m_connectedScreens.contains(sourceScreen)) {
    onDragCancelled(id);
    return;
  }
  if (action == QStringLiteral("dragStart") || action == QStringLiteral("transferOffer")) {
    qint64 total = 0;
    const auto manifest = message.value(QStringLiteral("files")).toArray();
    if (m_incomingAccepted && id == m_incomingId && sourceScreen == m_incomingSourceScreen &&
        manifest == m_incomingManifest) {
      return;
    }
    if (!validateTransferManifest(manifest, total) || !m_outgoingId.isEmpty() || m_incomingAccepted ||
        (!m_incomingId.isEmpty() && (id != m_incomingId || sourceScreen != m_incomingSourceScreen))) {
      sendDragMessage(
          {{QStringLiteral("action"), QStringLiteral("dragCancel")}, {QStringLiteral("transferId"), id}}, sourceScreen
      );
      return;
    }
    if (!m_incomingId.isEmpty() && manifest != m_incomingManifest) {
      cancelIncoming(tr("The incoming file offer changed"));
      return;
    }
    m_incomingId = id;
    m_incomingSourceScreen = sourceScreen;
    const QHostAddress advertised(message.value(QStringLiteral("senderIp")).toString());
    m_incomingSenderIp = advertised.isNull() ? QString() : advertised.toString();
    m_incomingManifest = manifest;
    m_incomingTimeout.start(kDragTimeout);
    m_remoteDragActive = action == QStringLiteral("dragStart");
    if (m_remoteDragActive) {
      showIncomingDrag();
    } else if (!m_offerDialog) {
      m_overlay->hideOverlay();
      auto *dialog = new QMessageBox(
          QMessageBox::Question, tr("Receive files with FlowPlus"),
          tr("Receive %1 files (%2) from %3?").arg(manifest.size()).arg(formatFileSize(total), sourceScreen),
          QMessageBox::Yes | QMessageBox::No
      );
      dialog->setAttribute(Qt::WA_DeleteOnClose);
      dialog->setTextFormat(Qt::PlainText);
      dialog->setDefaultButton(QMessageBox::No);
      m_offerDialog = dialog;
      connect(dialog, &QMessageBox::finished, this, [this, id](int result) {
        m_offerDialog = nullptr;
        if (id == m_incomingId && !m_incomingAccepted) {
          if (result == QMessageBox::Yes) {
            acceptIncomingTransfer(false, QPoint(-1, -1));
          } else {
            cancelIncoming();
          }
        }
      });
      dialog->open();
    }
    return;
  }
  if (action == QStringLiteral("dragCancel")) {
    if (id == m_incomingId && sourceScreen == m_incomingSourceScreen) {
      cancelIncoming(tr("The sender canceled the transfer"), false);
    } else if (id == m_outgoingId && sourceScreen == m_outgoingTargetScreen) {
      cancelOutgoing(tr("The destination declined or canceled the transfer"));
    }
    return;
  }
  if (id != m_outgoingId || m_outgoingId.isEmpty() ||
      (!m_outgoingTargetScreen.isEmpty() && sourceScreen != m_outgoingTargetScreen)) {
    return;
  }
  if (action == QStringLiteral("dropTrigger")) {
    if (m_outgoingSending || m_stagedFiles.isEmpty()) {
      return;
    }
    for (int index = 0; index < m_stagedFiles.size(); ++index) {
      if (QFileInfo(m_stagedFiles.at(index)).size() !=
          static_cast<qint64>(m_outgoingManifest.at(index).toObject().value(QStringLiteral("size")).toDouble(-1))) {
        cancelOutgoing(tr("A selected file changed during the drag"));
        return;
      }
    }
    m_outgoingTargetScreen = sourceScreen;
    m_outgoingSending = true;
    m_outgoingTimeout.start(kTransferTimeout);
    const QHostAddress receiver(message.value(QStringLiteral("receiverIp")).toString());
    const auto port = message.value(QStringLiteral("receiverPort")).toInt();
    // The HTTP grant binds our advertised IP. A relayed drag may use another
    // adapter to reach the final receiver; that transfer should use TLS directly.
    const QHostAddress routedSource(resolveLocalIpAddress(receiver.toString()));
    if (receiver.isNull() || (!receiver.isPrivateUse() && !receiver.isLoopback()) || port <= 0 || port > 65535 ||
        routedSource.isNull() || routedSource != QHostAddress(m_outgoingAdvertisedIp)) {
      sendFilesViaTunnel(m_stagedFiles);
    } else {
      m_localSend->sendFiles(
          receiver.toString(), static_cast<quint16>(port), m_stagedFiles, QPoint(-1, -1), m_outgoingId
      );
    }
    return;
  }
  if (!m_outgoingSending || !m_tunnelOutgoingFile ||
      message.value(QStringLiteral("fileIndex")).toInt(-1) != m_tunnelOutgoingIndex ||
      message.value(QStringLiteral("receivedBytes")).toDouble(-1) != m_tunnelOutgoingOffset) {
    return;
  }
  if ((action == QStringLiteral("tunnelReady") && m_tunnelPhase == TunnelPhase::AwaitingReady) ||
      (action == QStringLiteral("tunnelAck") && m_tunnelPhase == TunnelPhase::AwaitingChunk)) {
    m_outgoingTimeout.start(kTransferTimeout);
    sendNextTunnelChunk();
  } else if (action == QStringLiteral("tunnelComplete") && m_tunnelPhase == TunnelPhase::AwaitingComplete) {
    m_tunnelOutgoingFile.reset();
    ++m_tunnelOutgoingIndex;
    startNextTunnelFile();
  }
}

void DragDropManager::onOverlayDropDetected(const QPoint &screenPos)
{
  if (m_remoteDragActive && m_localScreenActive && !m_incomingAccepted) {
    acceptIncomingTransfer(true, screenPos);
  }
}

void DragDropManager::acceptIncomingTransfer(bool nativeDrop, const QPoint &dropPos)
{
  if (m_incomingId.isEmpty() || m_incomingAccepted) {
    return;
  }
  qint64 total = 0;
  if (!validateTransferManifest(m_incomingManifest, total)) {
    cancelIncoming(tr("Could not prepare the download folder"));
    return;
  }
  if (!prepareTransferDirectory(m_localSend->downloadDirectory(), total)) {
    cancelIncoming(tr("Could not prepare the download folder"));
    return;
  }
  m_incomingAccepted = true;
  m_remoteDragActive = false;
  m_incomingNativeDrop = nativeDrop;
  m_incomingDropPos = dropPos;
  if (nativeDrop) {
    captureNativeDropTarget();
  }
  const bool directAuthorized =
      m_localSend->authorizeIncomingTransfer(m_incomingSenderIp, m_incomingId, m_incomingManifest, dropPos);
  m_incomingTimeout.start(kTransferTimeout);
  m_overlay->showTransferring(tr("Connecting…"), 0);
  if (!sendDragMessage(
          {{QStringLiteral("action"), QStringLiteral("dropTrigger")},
           {QStringLiteral("transferId"), m_incomingId},
           {QStringLiteral("receiverIp"), resolveLocalIpAddress(m_incomingSenderIp)},
           {QStringLiteral("receiverPort"), directAuthorized ? m_localSend->serverPort() : 0}},
          m_incomingSourceScreen
      )) {
    cancelIncoming(tr("The sender is not available"));
  }
}

void DragDropManager::cancelOutgoing(const QString &reason)
{
  const auto id = m_outgoingId;
  m_outgoingId.clear();
  m_outgoingSending = false;
  if (!id.isEmpty()) {
    m_localSend->cancelOutgoingTransfer(id);
    sendDragMessage(
        {{QStringLiteral("action"), QStringLiteral("dragCancel")}, {QStringLiteral("transferId"), id}},
        m_outgoingTargetScreen
    );
    m_coreProcess.sendClearDrag();
  }
  m_outgoingTimeout.stop();
  m_outgoingTargetScreen.clear();
  m_outgoingAdvertisedIp.clear();
  m_stagedFiles.clear();
  m_outgoingManifest = {};
  m_tunnelOutgoingPaths.clear();
  m_tunnelOutgoingFile.reset();
  m_tunnelOutgoingIndex = 0;
  m_tunnelOutgoingOffset = 0;
  m_tunnelPhase = TunnelPhase::Idle;
  if (!reason.isEmpty()) {
    Q_EMIT statusMessage(reason);
  }
}

void DragDropManager::cancelIncoming(const QString &reason, bool notifyPeer)
{
  const auto id = m_incomingId;
  const auto sourceScreen = m_incomingSourceScreen;
  m_incomingId.clear();
  if (!id.isEmpty()) {
    if (notifyPeer) {
      sendDragMessage(
          {{QStringLiteral("action"), QStringLiteral("dragCancel")}, {QStringLiteral("transferId"), id}}, sourceScreen
      );
    }
    m_localSend->clearIncomingAuthorization(id);
  }
  if (m_offerDialog) {
    m_offerDialog->close();
    m_offerDialog = nullptr;
  }
  m_incomingTimeout.stop();
  m_incomingSourceScreen.clear();
  m_incomingSenderIp.clear();
  m_incomingManifest = {};
  m_remoteDragActive = false;
  m_incomingAccepted = false;
  m_incomingNativeDrop = false;
  m_tunnelIncomingFile.reset();
  m_tunnelIncomingFiles.clear();
  m_tunnelIncomingIndex = 0;
  m_tunnelIncomingReceivedBytes = 0;
  m_nativeDropTarget = 0;
  m_overlay->hideOverlay();
  if (!reason.isEmpty()) {
    Q_EMIT statusMessage(reason);
  }
}

void DragDropManager::finishOutgoing()
{
  // Do not send cancellation after the peer acknowledged successful receipt.
  m_outgoingId.clear();
  cancelOutgoing();
  m_coreProcess.sendClearDrag();
  Q_EMIT statusMessage(tr("Files received successfully by the destination"));
}

void DragDropManager::onLocalSendTransferFailed(
    const QString &reason, const QStringList &originalFiles, const QPoint &, bool allowFallback,
    const QString &transferId
)
{
  if (transferId != m_outgoingId || !m_outgoingSending) {
    return;
  }
  if (!allowFallback) {
    cancelOutgoing(reason);
    return;
  }
  Q_EMIT statusMessage(tr("Direct transfer unavailable; using the connected FlowPlus TLS channel"));
  sendFilesViaTunnel(originalFiles);
}

void DragDropManager::sendFilesViaTunnel(const QStringList &filePaths)
{
  if (!m_outgoingSending || filePaths != m_stagedFiles || !canTransfer()) {
    cancelOutgoing(tr("Cannot start the FlowPlus transfer"));
    return;
  }
  m_tunnelOutgoingPaths = filePaths;
  m_tunnelOutgoingIndex = 0;
  startNextTunnelFile();
}

void DragDropManager::startNextTunnelFile()
{
  if (m_tunnelOutgoingIndex >= m_tunnelOutgoingPaths.size()) {
    finishOutgoing();
    return;
  }
  m_tunnelOutgoingFile = std::make_unique<QFile>(m_tunnelOutgoingPaths.at(m_tunnelOutgoingIndex));
  const auto meta = m_outgoingManifest.at(m_tunnelOutgoingIndex).toObject();
  if (!m_tunnelOutgoingFile->open(QIODevice::ReadOnly) ||
      m_tunnelOutgoingFile->size() != static_cast<qint64>(meta.value(QStringLiteral("size")).toDouble(-1))) {
    cancelOutgoing(tr("Could not read the selected file, or its size changed"));
    return;
  }
  m_tunnelOutgoingOffset = 0;
  auto startMeta = meta;
  startMeta[QStringLiteral("transferId")] = m_outgoingId;
  startMeta[QStringLiteral("fileIndex")] = m_tunnelOutgoingIndex;
  m_tunnelPhase = TunnelPhase::AwaitingReady;
  m_outgoingTimeout.start(kTransferTimeout);
  if (!m_coreProcess.sendFileChunk(
          static_cast<uint8_t>(ChunkType::DataStart), QJsonDocument(startMeta).toJson(QJsonDocument::Compact),
          m_outgoingTargetScreen
      )) {
    cancelOutgoing(tr("The destination is not available"));
  }
}

void DragDropManager::sendNextTunnelChunk()
{
  const auto total = static_cast<qint64>(
      m_outgoingManifest.at(m_tunnelOutgoingIndex).toObject().value(QStringLiteral("size")).toDouble()
  );
  QByteArray payload;
  uint8_t mark;
  if (m_tunnelOutgoingOffset == total) {
    payload = QJsonDocument(
                  QJsonObject{
                      {QStringLiteral("transferId"), m_outgoingId}, {QStringLiteral("fileIndex"), m_tunnelOutgoingIndex}
                  }
    ).toJson(QJsonDocument::Compact);
    mark = static_cast<uint8_t>(ChunkType::DataEnd);
    m_tunnelPhase = TunnelPhase::AwaitingComplete;
  } else {
    const auto data = m_tunnelOutgoingFile->read(qMin(kChunkSize, total - m_tunnelOutgoingOffset));
    if (data.isEmpty()) {
      cancelOutgoing(tr("Could not read the entire file"));
      return;
    }
    payload = m_outgoingId.toUtf8();
    const auto networkOffset = qToBigEndian(static_cast<quint64>(m_tunnelOutgoingOffset));
    payload.append(reinterpret_cast<const char *>(&networkOffset), sizeof(networkOffset));
    payload.append(data);
    m_tunnelOutgoingOffset += data.size();
    mark = static_cast<uint8_t>(ChunkType::DataChunk);
    m_tunnelPhase = TunnelPhase::AwaitingChunk;
  }
  m_outgoingTimeout.start(kTransferTimeout);
  if (!m_coreProcess.sendFileChunk(mark, payload, m_outgoingTargetScreen)) {
    cancelOutgoing(tr("The destination is not available"));
    return;
  }
  Q_EMIT transferProgressUpdated(
      QFileInfo(m_tunnelOutgoingPaths.at(m_tunnelOutgoingIndex)).fileName(),
      transferProgressPercent(m_tunnelOutgoingOffset, total)
  );
}

bool DragDropManager::acknowledgeTunnel(const QString &action)
{
  return sendDragMessage(
      {{QStringLiteral("action"), action},
       {QStringLiteral("transferId"), m_incomingId},
       {QStringLiteral("fileIndex"), m_tunnelIncomingIndex},
       {QStringLiteral("receivedBytes"), m_tunnelIncomingReceivedBytes}},
      m_incomingSourceScreen
  );
}

void DragDropManager::onFileChunkReceived(const QString &sourceScreen, uint8_t mark, const QByteArray &chunk)
{
  if (!m_incomingAccepted || m_incomingId.isEmpty() || sourceScreen != m_incomingSourceScreen || !canTransfer()) {
    return;
  }
  if (mark == static_cast<uint8_t>(ChunkType::DataStart)) {
    const auto doc = QJsonDocument::fromJson(chunk);
    const auto meta = doc.object();
    if (chunk.size() > 2048 || !doc.isObject() || meta.value(QStringLiteral("transferId")).toString() != m_incomingId ||
        meta.value(QStringLiteral("fileIndex")).toInt(-1) != m_tunnelIncomingIndex || m_tunnelIncomingFile ||
        m_tunnelIncomingIndex >= m_incomingManifest.size()) {
      cancelIncoming(tr("Invalid file transfer sequence"));
      return;
    }
    const auto expected = m_incomingManifest.at(m_tunnelIncomingIndex).toObject();
    if (meta.value(QStringLiteral("fileName")) != expected.value(QStringLiteral("fileName")) ||
        meta.value(QStringLiteral("size")) != expected.value(QStringLiteral("size"))) {
      cancelIncoming(tr("The file does not match the accepted offer"));
      return;
    }
    m_localSend->clearIncomingAuthorization(m_incomingId);
    m_tunnelIncomingFile = std::make_unique<QTemporaryFile>(
        QDir(m_localSend->downloadDirectory()).filePath(QStringLiteral(".flowplus-XXXXXX.part"))
    );
    if (!m_tunnelIncomingFile->open()) {
      cancelIncoming(tr("Could not create the download file"));
      return;
    }
    m_tunnelIncomingReceivedBytes = 0;
    m_incomingTimeout.start(kTransferTimeout);
    m_overlay->showTransferring(expected.value(QStringLiteral("fileName")).toString(), 0);
    if (!acknowledgeTunnel(QStringLiteral("tunnelReady"))) {
      cancelIncoming(tr("The sender is not available"), false);
    }
    return;
  }
  if (!m_tunnelIncomingFile) {
    return;
  }
  const auto expected = m_incomingManifest.at(m_tunnelIncomingIndex).toObject();
  const auto total = static_cast<qint64>(expected.value(QStringLiteral("size")).toDouble());
  if (mark == static_cast<uint8_t>(ChunkType::DataChunk)) {
    if (chunk.size() <= kChunkHeaderSize || chunk.size() > kChunkHeaderSize + kChunkSize ||
        chunk.left(kUuidLength) != m_incomingId.toUtf8() ||
        qFromBigEndian<quint64>(reinterpret_cast<const uchar *>(chunk.constData() + kUuidLength)) !=
            static_cast<quint64>(m_tunnelIncomingReceivedBytes) ||
        chunk.size() - kChunkHeaderSize > total - m_tunnelIncomingReceivedBytes) {
      cancelIncoming(tr("Invalid file data or size"));
      return;
    }
    const auto dataSize = chunk.size() - kChunkHeaderSize;
    if (m_tunnelIncomingFile->write(chunk.constData() + kChunkHeaderSize, dataSize) != dataSize) {
      cancelIncoming(tr("Could not write the download file"));
      return;
    }
    m_tunnelIncomingReceivedBytes += dataSize;
    m_incomingTimeout.start(kTransferTimeout);
    const auto percent = transferProgressPercent(m_tunnelIncomingReceivedBytes, total);
    m_overlay->showTransferring(expected.value(QStringLiteral("fileName")).toString(), percent);
    Q_EMIT transferProgressUpdated(expected.value(QStringLiteral("fileName")).toString(), percent);
    if (!acknowledgeTunnel(QStringLiteral("tunnelAck"))) {
      cancelIncoming(tr("The sender is not available"), false);
    }
    return;
  }
  if (mark != static_cast<uint8_t>(ChunkType::DataEnd)) {
    cancelIncoming(tr("Unknown file transfer message"));
    return;
  }
  const auto end = QJsonDocument::fromJson(chunk).object();
  if (chunk.size() > 2048 || end.value(QStringLiteral("transferId")).toString() != m_incomingId ||
      end.value(QStringLiteral("fileIndex")).toInt(-1) != m_tunnelIncomingIndex ||
      m_tunnelIncomingReceivedBytes != total || !m_tunnelIncomingFile->flush()) {
    cancelIncoming(tr("The received file is incomplete"));
    return;
  }
  m_tunnelIncomingFile->close();
  m_tunnelIncomingFiles.push_back(std::move(m_tunnelIncomingFile));
  if (m_tunnelIncomingIndex + 1 == m_incomingManifest.size()) {
    QStringList paths;
    if (!publishTunnelFiles(paths)) {
      cancelIncoming(tr("Could not save the received files"));
      return;
    }
    const auto id = m_incomingId;
    const auto pos = m_incomingDropPos;
    const bool acknowledged = acknowledgeTunnel(QStringLiteral("tunnelComplete"));
    handleCompletedFiles(paths, pos, id);
    if (!acknowledged) {
      Q_EMIT statusMessage(tr("Files saved; the sender disconnected before receiving confirmation"));
    }
  } else {
    if (!acknowledgeTunnel(QStringLiteral("tunnelComplete"))) {
      cancelIncoming(tr("The sender is not available"), false);
      return;
    }
    ++m_tunnelIncomingIndex;
    m_tunnelIncomingReceivedBytes = 0;
    m_incomingTimeout.start(kTransferTimeout);
  }
}

bool DragDropManager::publishTunnelFiles(QStringList &savedPaths)
{
  for (size_t index = 0; index < m_tunnelIncomingFiles.size(); ++index) {
    const auto name =
        m_incomingManifest.at(static_cast<qsizetype>(index)).toObject().value(QStringLiteral("fileName")).toString();
    const auto destination = uniqueTransferDestination(m_localSend->downloadDirectory(), name);
    if (destination.isEmpty() || !m_tunnelIncomingFiles[index]->rename(destination)) {
      // Already-renamed files still auto-remove until the entire batch commits.
      return false;
    }
    savedPaths.append(destination);
  }
  for (auto &file : m_tunnelIncomingFiles) {
    file->setAutoRemove(false);
  }
  m_tunnelIncomingFiles.clear();
  return true;
}

void DragDropManager::handleCompletedFiles(const QStringList &savedPaths, const QPoint &, const QString &transferId)
{
  if (savedPaths.isEmpty() || transferId != m_incomingId || !m_incomingAccepted) {
    return;
  }
  const bool nativeDrop = m_incomingNativeDrop;
  const bool delivered = nativeDrop && injectNativeDrop(savedPaths);
  cancelIncoming({}, false);
  m_overlay->showCompleted(tr("Files received: %1").arg(savedPaths.size()));
  Q_EMIT statusMessage(
      delivered    ? tr("Files received; native drop requested")
      : nativeDrop ? tr("Files saved in Downloads/FlowPlus and available to paste")
                   : tr("Files saved in Downloads/FlowPlus")
  );
  Q_EMIT filesReceived(savedPaths);
}

void DragDropManager::captureNativeDropTarget()
{
#if defined(Q_OS_WIN)
  POINT point;
  if (!GetCursorPos(&point)) {
    return;
  }
  const auto target = WindowFromPoint(point);
  if (!target) {
    return;
  }
  wchar_t className[256] = {};
  GetClassNameW(target, className, 256);
  DWORD process = 0;
  GetWindowThreadProcessId(target, &process);
  m_nativeDropTarget = reinterpret_cast<quintptr>(target);
  m_nativeDropProcess = process;
  m_nativeDropClass = QString::fromWCharArray(className);
  m_nativeDropPoint = QPoint(point.x, point.y);
#endif
}

bool DragDropManager::injectNativeDrop(const QStringList &savedPaths)
{
  auto *mime = new QMimeData;
  QList<QUrl> urls;
  for (const auto &path : savedPaths) {
    urls.append(QUrl::fromLocalFile(path));
  }
  mime->setUrls(urls);
  QGuiApplication::clipboard()->setMimeData(mime);

#if defined(Q_OS_WIN)
  auto target = reinterpret_cast<HWND>(m_nativeDropTarget);
  DWORD process = 0;
  wchar_t className[256] = {};
  if (!target || !IsWindow(target) || !IsWindowVisible(target)) {
    return false;
  }
  GetWindowThreadProcessId(target, &process);
  GetClassNameW(target, className, 256);
  if (process != m_nativeDropProcess || QString::fromWCharArray(className) != m_nativeDropClass) {
    return false;
  }
  DWORD shellProcess = 0;
  GetWindowThreadProcessId(GetShellWindow(), &shellProcess);
  bool isDesktop = false;
  for (auto ancestor = target; ancestor; ancestor = GetParent(ancestor)) {
    GetClassNameW(ancestor, className, 256);
    const auto name = QString::fromWCharArray(className);
    isDesktop = process == shellProcess && (name == QStringLiteral("SHELLDLL_DefView") ||
                                            name == QStringLiteral("Progman") || name == QStringLiteral("WorkerW"));
    if (isDesktop) {
      break;
    }
  }
  if (isDesktop) {
    const auto desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (desktop.isEmpty()) {
      return false;
    }
    QStringList copied;
    for (const auto &source : savedPaths) {
      const auto destination = uniqueTransferDestination(desktop, QFileInfo(source).fileName());
      if (destination.isEmpty() || !QFile::copy(source, destination)) {
        for (const auto &path : copied) {
          QFile::remove(path);
        }
        return false;
      }
      copied.append(destination);
    }
    return true;
  }
  // WM_DROPFILES is a legacy opt-in API. Modern OLE targets may not support it.
  while (target && !(GetWindowLongPtrW(target, GWL_EXSTYLE) & WS_EX_ACCEPTFILES)) {
    target = GetParent(target);
  }
  if (!target) {
    return false;
  }
  QByteArray paths;
  for (const auto &path : savedPaths) {
    const auto native = QDir::toNativeSeparators(path);
    paths.append(reinterpret_cast<const char *>(native.utf16()), (native.size() + 1) * sizeof(wchar_t));
  }
  paths.append(sizeof(wchar_t), '\0');
  const auto memory = GlobalAlloc(GHND, sizeof(DROPFILES) + paths.size());
  if (!memory) {
    return false;
  }
  auto *drop = static_cast<DROPFILES *>(GlobalLock(memory));
  if (!drop) {
    GlobalFree(memory);
    return false;
  }
  drop->pFiles = sizeof(DROPFILES);
  drop->pt = POINT{m_nativeDropPoint.x(), m_nativeDropPoint.y()};
  ScreenToClient(target, &drop->pt);
  drop->fNC = FALSE;
  drop->fWide = TRUE;
  std::memcpy(reinterpret_cast<char *>(drop) + sizeof(DROPFILES), paths.constData(), paths.size());
  GlobalUnlock(memory);
  if (!PostMessageW(target, WM_DROPFILES, reinterpret_cast<WPARAM>(memory), 0)) {
    GlobalFree(memory);
    return false;
  }
  return true;
#else
  // Finder requires a native drag session; received URLs are available to paste.
  return false;
#endif
}

} // namespace deskflow::gui::transfer
