/*
 * FlowPlus -- mouse, keyboard, and file sharing utility
 * SPDX-FileCopyrightText: (C) 2026 FlowPlus Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "DragDropManager.h"

#include "common/Settings.h"
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
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QScreen>
#include <QStandardPaths>
#include <QUrl>

#if defined(Q_OS_WIN)
#define WIN32_LEAN_AND_MEAN
#include <ShlObj.h>
#include <Windows.h>
#include <shellapi.h>
#endif

namespace deskflow::gui::transfer {

namespace {
QString extractLocalFilesFromMime(const QMimeData *mime, QStringList &outFiles)
{
  if (!mime || !mime->hasUrls()) {
    return {};
  }
  for (const QUrl &url : mime->urls()) {
    if (url.isLocalFile()) {
      const QString localPath = url.toLocalFile();
      if (QFileInfo(localPath).isFile() && !outFiles.contains(localPath)) {
        outFiles.append(localPath);
      }
    }
  }
  return outFiles.isEmpty() ? QString() : outFiles.first();
}
} // namespace

//
// EdgeDropSensor
//

EdgeDropSensor::EdgeDropSensor(Edge edge, DragDropManager *manager, QWidget *parent)
    : QWidget(
          parent,
          Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus
      ),
      m_edge(edge),
      m_manager(manager)
{
  setAcceptDrops(true);
  setAttribute(Qt::WA_ShowWithoutActivating, true);
  setAttribute(Qt::WA_TranslucentBackground, true);
  setWindowOpacity(0.01);
}

void EdgeDropSensor::updateGeometryForScreen(const QRect &screenRect)
{
  constexpr int kThickness = 3;
  switch (m_edge) {
  case Edge::Left:
    setGeometry(screenRect.left(), screenRect.top(), kThickness, screenRect.height());
    break;
  case Edge::Right:
    setGeometry(screenRect.right() - kThickness + 1, screenRect.top(), kThickness, screenRect.height());
    break;
  case Edge::Top:
    setGeometry(screenRect.left(), screenRect.top(), screenRect.width(), kThickness);
    break;
  case Edge::Bottom:
    setGeometry(screenRect.left(), screenRect.bottom() - kThickness + 1, screenRect.width(), kThickness);
    break;
  }
}

void EdgeDropSensor::dragEnterEvent(QDragEnterEvent *event)
{
  QStringList files;
  if (!extractLocalFilesFromMime(event->mimeData(), files).isEmpty()) {
    event->acceptProposedAction();
    if (m_manager) {
      m_manager->stageFilesForDrag(files, true);
    }
  }
}

void EdgeDropSensor::dragMoveEvent(QDragMoveEvent *event)
{
  if (event->mimeData() && event->mimeData()->hasUrls()) {
    event->acceptProposedAction();
  }
}

void EdgeDropSensor::dropEvent(QDropEvent *event)
{
  QStringList files;
  if (!extractLocalFilesFromMime(event->mimeData(), files).isEmpty()) {
    event->acceptProposedAction();
    if (m_manager) {
      m_manager->triggerImmediateSend(files, QCursor::pos());
    }
  }
}

//
// DragDropManager
//

DragDropManager::DragDropManager(deskflow::gui::CoreProcess &coreProcess, QObject *parent)
    : QObject(parent),
      m_coreProcess(coreProcess),
      m_localSend{new LocalSendService(this)},
      m_overlay{new DragOverlayWidget()}
{
  m_localSend->start(kLocalSendDefaultPort);

  m_edgeSensors.append(new EdgeDropSensor(EdgeDropSensor::Edge::Left, this));
  m_edgeSensors.append(new EdgeDropSensor(EdgeDropSensor::Edge::Right, this));
  m_edgeSensors.append(new EdgeDropSensor(EdgeDropSensor::Edge::Top, this));
  m_edgeSensors.append(new EdgeDropSensor(EdgeDropSensor::Edge::Bottom, this));
  updateEdgeSensors();

  if (auto *primary = QGuiApplication::primaryScreen()) {
    connect(primary, &QScreen::geometryChanged, this, [this](const QRect &) { updateEdgeSensors(); });
  }

  connect(&m_coreProcess, &CoreProcess::screenSwitched, this, &DragDropManager::onScreenSwitched);
  connect(&m_coreProcess, &CoreProcess::clientScreenEntered, this, &DragDropManager::onClientScreenEntered);
  connect(&m_coreProcess, &CoreProcess::clientScreenLeft, this, &DragDropManager::onClientScreenLeft);
  connect(&m_coreProcess, &CoreProcess::mouseButtonUp, this, &DragDropManager::onMouseButtonUp);
  connect(&m_coreProcess, &CoreProcess::dragInfoReceived, this, &DragDropManager::onDragInfoReceived);
  connect(&m_coreProcess, &CoreProcess::fileChunkReceived, this, &DragDropManager::onFileChunkReceived);

  connect(m_overlay, &DragOverlayWidget::dropDetected, this, &DragDropManager::onOverlayDropDetected);

  connect(m_localSend, &LocalSendService::deviceDiscovered, this, [this](const LocalSendDevice &dev) {
    if (m_knownPeerIp.isEmpty() && !dev.ipAddress.isEmpty()) {
      m_knownPeerIp = dev.ipAddress;
    }
    Q_EMIT statusMessage(tr("Dispositivo LocalSend rilevato: %1 (%2)").arg(dev.alias, dev.ipAddress));
  });

  connect(
      m_localSend, &LocalSendService::transferStarted, this,
      [this](const QString &, const QStringList &fileNames, qint64) {
        const QString label = fileNames.isEmpty() ? tr("File") : fileNames.first();
        m_overlay->showTransferring(label, 0);
        Q_EMIT statusMessage(tr("Ricezione file in corso: %1").arg(fileNames.join(QStringLiteral(", "))));
      }
  );

  connect(
      m_localSend, &LocalSendService::transferProgress, this,
      [this](const QString &fileName, qint64 transferred, qint64 total) {
        const int pct = total > 0 ? static_cast<int>((transferred * 100) / total) : 0;
        if (m_overlay->isVisible()) {
          m_overlay->showTransferring(fileName, pct);
        }
        Q_EMIT transferProgressUpdated(fileName, pct);
      }
  );

  connect(
      m_localSend, &LocalSendService::transferCompleted, this,
      [this](const QStringList &paths, const QPoint &dropPos) { handleCompletedFiles(paths, dropPos); }
  );

  connect(m_localSend, &LocalSendService::transferFailed, this, &DragDropManager::onLocalSendTransferFailed);
}

DragDropManager::~DragDropManager()
{
  for (auto *sensor : m_edgeSensors) {
    sensor->hide();
    delete sensor;
  }
  m_edgeSensors.clear();

  if (m_tunnelIncomingFile) {
    m_tunnelIncomingFile->close();
    delete m_tunnelIncomingFile;
    m_tunnelIncomingFile = nullptr;
  }

  delete m_overlay;
}

void DragDropManager::updateEdgeSensors()
{
  QScreen *screen = QGuiApplication::primaryScreen();
  if (!screen) {
    return;
  }
  const QRect geom = screen->geometry();
  for (auto *sensor : m_edgeSensors) {
    sensor->updateGeometryForScreen(geom);
    sensor->show();
  }
}

void DragDropManager::setKnownPeerAddress(const QString &peerAddress)
{
  QString cleaned = peerAddress.trimmed();
  if (cleaned.contains(':')) {
    const auto parts = cleaned.split(':');
    if (parts.size() == 2) {
      cleaned = parts.first();
    }
  }
  if (!cleaned.isEmpty()) {
    m_knownPeerIp = cleaned;
  }
}

QString DragDropManager::resolveLocalIpAddress() const
{
  const QStringList validIps = NetworkMonitor::validAddresses();
  if (!validIps.isEmpty()) {
    return validIps.first();
  }
  return QStringLiteral("127.0.0.1");
}

QString DragDropManager::formatFileSize(qint64 bytes)
{
  if (bytes < 1024) {
    return QStringLiteral("%1 B").arg(bytes);
  }
  if (bytes < 1024 * 1024) {
    return QStringLiteral("%1 KB").arg(QString::number(bytes / 1024.0, 'f', 1));
  }
  return QStringLiteral("%1 MB").arg(QString::number(bytes / (1024.0 * 1024.0), 'f', 1));
}

void DragDropManager::stageFilesForDrag(const QStringList &filePaths, bool notifyPeerImmediately)
{
  QStringList valid;
  qint64 totalBytes = 0;
  QJsonArray filesArr;

  for (const QString &path : filePaths) {
    const QFileInfo fi(path);
    if (fi.exists() && fi.isFile()) {
      valid.append(fi.absoluteFilePath());
      totalBytes += fi.size();
      QJsonObject fObj;
      fObj[QStringLiteral("fileName")] = fi.fileName();
      fObj[QStringLiteral("size")] = fi.size();
      filesArr.append(fObj);
    }
  }

  if (valid.isEmpty()) {
    return;
  }

  m_stagedFiles = valid;
  Q_EMIT statusMessage(
      tr("File pronti per il trascinamento cross-device: %1 (%2)")
          .arg(QFileInfo(valid.first()).fileName(), formatFileSize(totalBytes))
  );

  if (notifyPeerImmediately) {
    QJsonObject msg;
    msg[QStringLiteral("action")] = QStringLiteral("dragStart");
    msg[QStringLiteral("senderIp")] = resolveLocalIpAddress();
    msg[QStringLiteral("senderPort")] = static_cast<int>(m_localSend->serverPort());
    msg[QStringLiteral("files")] = filesArr;
    msg[QStringLiteral("totalBytes")] = totalBytes;

    const QByteArray payload = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    m_coreProcess.sendStartDrag(payload);
  }
}

void DragDropManager::triggerImmediateSend(const QStringList &filePaths, const QPoint &dropPos)
{
  stageFilesForDrag(filePaths, true);

  QString peerIp = m_knownPeerIp;
  if (peerIp.isEmpty()) {
    const QString remoteHost = Settings::value(Settings::Client::RemoteHost).toString();
    if (!remoteHost.isEmpty()) {
      peerIp = remoteHost.split(':').first();
    } else if (!m_localSend->discoveredDevices().isEmpty()) {
      peerIp = m_localSend->discoveredDevices().first().ipAddress;
    }
  }

  if (!peerIp.isEmpty()) {
    Q_EMIT statusMessage(tr("Avvio trasferimento LocalSend verso %1...").arg(peerIp));
    m_localSend->sendFiles(peerIp, kLocalSendDefaultPort, m_stagedFiles, dropPos);
  } else {
    // Use active FlowPlus TCP/TLS stream directly
    sendFilesViaTunnel(m_stagedFiles, dropPos);
  }
}

void DragDropManager::onScreenSwitched(const QString &screenName, bool isPrimary, int x, int y)
{
  Q_UNUSED(screenName)
  Q_UNUSED(x)
  Q_UNUSED(y)

  if (!isPrimary) {
    // If the user copied files to clipboard before crossing the screen edge while dragging, stage them automatically
    if (m_stagedFiles.isEmpty()) {
      QStringList clipFiles;
      if (!extractLocalFilesFromMime(QGuiApplication::clipboard()->mimeData(), clipFiles).isEmpty() &&
          (QGuiApplication::mouseButtons() & Qt::LeftButton)) {
        stageFilesForDrag(clipFiles, true);
      }
    }
  } else {
    if (m_remoteDragActive) {
      m_remoteDragActive = false;
      m_overlay->hideOverlay();
    }
  }
}

void DragDropManager::onClientScreenEntered(int x, int y)
{
  Q_UNUSED(x)
  Q_UNUSED(y)
}

void DragDropManager::onClientScreenLeft()
{
  if (m_remoteDragActive) {
    m_remoteDragActive = false;
    m_overlay->hideOverlay();
  }
}

void DragDropManager::onMouseButtonUp(const QString &screenName, int x, int y)
{
  Q_UNUSED(screenName)
  if (m_remoteDragActive && m_overlay->state() == DragOverlayWidget::State::Dragging) {
    onOverlayDropDetected(QPoint(x, y));
  }
}

void DragDropManager::onDragInfoReceived(uint32_t fileCount, const QByteArray &payload)
{
  Q_UNUSED(fileCount)
  const QJsonDocument doc = QJsonDocument::fromJson(payload);
  if (!doc.isObject()) {
    return;
  }

  const QJsonObject obj = doc.object();
  const QString action = obj.value(QStringLiteral("action")).toString();

  if (action == QStringLiteral("dragStart")) {
    m_incomingSenderIp = obj.value(QStringLiteral("senderIp")).toString();
    m_incomingSenderPort =
        static_cast<quint16>(obj.value(QStringLiteral("senderPort")).toInt(kLocalSendDefaultPort));
    if (!m_incomingSenderIp.isEmpty()) {
      m_knownPeerIp = m_incomingSenderIp;
    }

    const QJsonArray filesArr = obj.value(QStringLiteral("files")).toArray();
    const qint64 totalBytes = static_cast<qint64>(obj.value(QStringLiteral("totalBytes")).toDouble(0));
    if (filesArr.isEmpty()) {
      return;
    }

    const QString firstFile = filesArr.first().toObject().value(QStringLiteral("fileName")).toString(tr("File"));
    const QString summary = filesArr.size() == 1
                                ? QStringLiteral("📄 %1 (%2)").arg(firstFile, formatFileSize(totalBytes))
                                : tr("📄 %1 file (%2)").arg(filesArr.size()).arg(formatFileSize(totalBytes));

    m_remoteDragActive = true;
    m_overlay->showDragging(summary, tr("Rilascia il mouse per scaricare qui via FlowPlus"));
    Q_EMIT statusMessage(tr("Trascinamento file in ingresso da %1: %2").arg(m_incomingSenderIp, summary));
    return;
  }

  if (action == QStringLiteral("dropTrigger")) {
    const QString receiverIp = obj.value(QStringLiteral("receiverIp")).toString();
    const quint16 receiverPort =
        static_cast<quint16>(obj.value(QStringLiteral("receiverPort")).toInt(kLocalSendDefaultPort));
    const QPoint dropPos(obj.value(QStringLiteral("dropX")).toInt(-1), obj.value(QStringLiteral("dropY")).toInt(-1));

    if (!m_stagedFiles.isEmpty()) {
      const QString targetHost = !receiverIp.isEmpty() ? receiverIp : m_knownPeerIp;
      const QStringList filesToTransfer = m_stagedFiles;
      m_stagedFiles.clear();
      m_coreProcess.sendClearDrag();

      if (!targetHost.isEmpty()) {
        Q_EMIT statusMessage(tr("Rilascio rilevato! Invio file via LocalSend a %1...").arg(targetHost));
        m_localSend->sendFiles(targetHost, receiverPort, filesToTransfer, dropPos);
      } else {
        sendFilesViaTunnel(filesToTransfer, dropPos);
      }
    }
  }
}

void DragDropManager::onOverlayDropDetected(const QPoint &screenPos)
{
  if (!m_remoteDragActive) {
    return;
  }
  m_remoteDragActive = false;
  m_overlay->showTransferring(tr("Connessione in corso..."), 0);

  QJsonObject dropMsg;
  dropMsg[QStringLiteral("action")] = QStringLiteral("dropTrigger");
  dropMsg[QStringLiteral("receiverIp")] = resolveLocalIpAddress();
  dropMsg[QStringLiteral("receiverPort")] = static_cast<int>(m_localSend->serverPort());
  dropMsg[QStringLiteral("dropX")] = screenPos.x();
  dropMsg[QStringLiteral("dropY")] = screenPos.y();

  const QByteArray payload = QJsonDocument(dropMsg).toJson(QJsonDocument::Compact);
  m_coreProcess.sendStartDrag(payload);
}

void DragDropManager::onLocalSendTransferFailed(
    const QString &reason, const QStringList &originalFiles, const QPoint &dropPos
)
{
  qWarning() << "LocalSend direct HTTP transfer failed (" << reason << "), falling back to FlowPlus TLS tunnel";
  Q_EMIT statusMessage(tr("Fallback su tunnel crittografato FlowPlus in corso..."));
  sendFilesViaTunnel(originalFiles, dropPos);
}

void DragDropManager::sendFilesViaTunnel(const QStringList &filePaths, const QPoint &dropPos)
{
  constexpr qint64 kChunkSize = 32 * 1024;
  for (const QString &filePath : filePaths) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
      continue;
    }

    const QFileInfo fi(filePath);
    QJsonObject startMeta;
    startMeta[QStringLiteral("fileName")] = fi.fileName();
    startMeta[QStringLiteral("size")] = fi.size();
    startMeta[QStringLiteral("dropX")] = dropPos.x();
    startMeta[QStringLiteral("dropY")] = dropPos.y();

    m_coreProcess.sendFileChunk(
        static_cast<uint8_t>(kDataStart), QJsonDocument(startMeta).toJson(QJsonDocument::Compact)
    );

    qint64 sent = 0;
    const qint64 total = fi.size();
    while (!file.atEnd()) {
      const QByteArray chunk = file.read(kChunkSize);
      if (chunk.isEmpty()) {
        break;
      }
      m_coreProcess.sendFileChunk(static_cast<uint8_t>(kDataChunk), chunk);
      sent += chunk.size();
      const int pct = total > 0 ? static_cast<int>((sent * 100) / total) : 100;
      Q_EMIT transferProgressUpdated(fi.fileName(), pct);
    }

    m_coreProcess.sendFileChunk(static_cast<uint8_t>(kDataEnd), QByteArray());
    file.close();
  }

  Q_EMIT statusMessage(tr("Trasferimento file completato tramite tunnel FlowPlus"));
}

void DragDropManager::onFileChunkReceived(uint8_t mark, const QByteArray &chunk)
{
  if (mark == static_cast<uint8_t>(kDataStart)) {
    if (m_tunnelIncomingFile) {
      m_tunnelIncomingFile->close();
      delete m_tunnelIncomingFile;
      m_tunnelIncomingFile = nullptr;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(chunk);
    const QJsonObject meta = doc.object();
    m_tunnelIncomingFileName =
        QFileInfo(meta.value(QStringLiteral("fileName")).toString(QStringLiteral("received_file"))).fileName();
    m_tunnelIncomingTotalBytes = static_cast<qint64>(meta.value(QStringLiteral("size")).toDouble(0));
    m_tunnelIncomingReceivedBytes = 0;
    m_tunnelIncomingDropPos =
        QPoint(meta.value(QStringLiteral("dropX")).toInt(-1), meta.value(QStringLiteral("dropY")).toInt(-1));

    const QString dir = m_localSend->downloadDirectory();
    QDir().mkpath(dir);
    m_tunnelIncomingFilePath = QDir(dir).filePath(m_tunnelIncomingFileName);

    m_tunnelIncomingFile = new QFile(m_tunnelIncomingFilePath);
    if (!m_tunnelIncomingFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      delete m_tunnelIncomingFile;
      m_tunnelIncomingFile = nullptr;
      return;
    }

    m_overlay->showTransferring(m_tunnelIncomingFileName, 0);
    return;
  }

  if (mark == static_cast<uint8_t>(kDataChunk)) {
    if (!m_tunnelIncomingFile) {
      return;
    }
    m_tunnelIncomingFile->write(chunk);
    m_tunnelIncomingReceivedBytes += chunk.size();
    const int pct = m_tunnelIncomingTotalBytes > 0
                        ? static_cast<int>((m_tunnelIncomingReceivedBytes * 100) / m_tunnelIncomingTotalBytes)
                        : 50;
    m_overlay->showTransferring(m_tunnelIncomingFileName, pct);
    Q_EMIT transferProgressUpdated(m_tunnelIncomingFileName, pct);
    return;
  }

  if (mark == static_cast<uint8_t>(kDataEnd)) {
    if (!m_tunnelIncomingFile) {
      return;
    }
    m_tunnelIncomingFile->flush();
    m_tunnelIncomingFile->close();
    delete m_tunnelIncomingFile;
    m_tunnelIncomingFile = nullptr;

    handleCompletedFiles({m_tunnelIncomingFilePath}, m_tunnelIncomingDropPos);
  }
}

void DragDropManager::handleCompletedFiles(const QStringList &savedPaths, const QPoint &dropPos)
{
  if (savedPaths.isEmpty()) {
    return;
  }

  const QPoint effectiveDropPos = (dropPos.x() >= 0 && dropPos.y() >= 0) ? dropPos : QCursor::pos();
  injectNativeDrop(savedPaths, effectiveDropPos);

  const QString firstName = QFileInfo(savedPaths.first()).fileName();
  m_overlay->showCompleted(tr("✓ File ricevuto: %1").arg(firstName));
  Q_EMIT statusMessage(tr("File ricevuto e rilasciato: %1").arg(savedPaths.join(QStringLiteral(", "))));
  Q_EMIT filesReceived(savedPaths);
}

void DragDropManager::injectNativeDrop(const QStringList &savedPaths, const QPoint &dropPos)
{
  // 1. Place the received file(s) as native file URLs on the system clipboard
  auto *mimeData = new QMimeData();
  QList<QUrl> urls;
  for (const QString &path : savedPaths) {
    urls.append(QUrl::fromLocalFile(path));
  }
  mimeData->setUrls(urls);
  QGuiApplication::clipboard()->setMimeData(mimeData);

#if defined(Q_OS_WIN)
  // 2. Inspect target window under the drop coordinates on Windows
  POINT pt{dropPos.x(), dropPos.y()};
  HWND hwndTarget = WindowFromPoint(pt);
  if (hwndTarget) {
    wchar_t className[128] = {0};
    GetClassNameW(hwndTarget, className, 127);
    const QString wndClass = QString::fromWCharArray(className);

    // If dropped onto the Windows Desktop, also copy the file directly to Desktop
    if (wndClass == QStringLiteral("SysListView32") || wndClass == QStringLiteral("SHELLDLL_DefView") ||
        wndClass == QStringLiteral("WorkerW") || wndClass == QStringLiteral("Progman")) {
      const QString desktopDir = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
      if (!desktopDir.isEmpty()) {
        for (const QString &srcPath : savedPaths) {
          const QString dstPath = QDir(desktopDir).filePath(QFileInfo(srcPath).fileName());
          if (QFileInfo::exists(dstPath)) {
            QFile::remove(dstPath);
          }
          QFile::copy(srcPath, dstPath);
        }
      }
    }

    // Build and post native WM_DROPFILES structure to the target window
    QByteArray widePathsBuffer;
    for (const QString &path : savedPaths) {
      const QString nativePath = QDir::toNativeSeparators(path);
      widePathsBuffer.append(
          reinterpret_cast<const char *>(nativePath.utf16()), (nativePath.size() + 1) * static_cast<int>(sizeof(wchar_t))
      );
    }
    const wchar_t nullTerm = 0;
    widePathsBuffer.append(reinterpret_cast<const char *>(&nullTerm), sizeof(wchar_t));

    const SIZE_T totalSize = sizeof(DROPFILES) + static_cast<SIZE_T>(widePathsBuffer.size());
    HGLOBAL hGlobal = GlobalAlloc(GHND | GMEM_SHARE, totalSize);
    if (hGlobal) {
      auto *dropFiles = static_cast<DROPFILES *>(GlobalLock(hGlobal));
      if (dropFiles) {
        dropFiles->pFiles = sizeof(DROPFILES);
        dropFiles->pt = pt;
        ScreenToClient(hwndTarget, &dropFiles->pt);
        dropFiles->fNC = FALSE;
        dropFiles->fWide = TRUE;
        memcpy(reinterpret_cast<char *>(dropFiles) + sizeof(DROPFILES), widePathsBuffer.constData(), widePathsBuffer.size());
        GlobalUnlock(hGlobal);
        if (!PostMessageW(hwndTarget, WM_DROPFILES, reinterpret_cast<WPARAM>(hGlobal), 0)) {
          GlobalFree(hGlobal);
        }
      } else {
        GlobalFree(hGlobal);
      }
    }
  }
#endif
}

} // namespace deskflow::gui::transfer
