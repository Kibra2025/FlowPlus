/*
 * FlowPlus -- mouse, keyboard, and file sharing utility
 * SPDX-FileCopyrightText: (C) 2026 FlowPlus Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "LocalSendService.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMimeDatabase>
#include <QNetworkDatagram>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QSysInfo>
#include <QUrlQuery>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <utility>

namespace deskflow::gui::transfer {
namespace {

constexpr qint64 kMaxHeaderBytes = 16 * 1024;
constexpr qint64 kMaxJsonBytes = 1024 * 1024;
constexpr qint64 kChunkBytes = 64 * 1024;
constexpr qint64 kIdleTimeoutMs = 60000;
constexpr qint64 kGrantLifetimeMs = 120000;
constexpr int kMaxConnections = 16;
constexpr int kMaxSessions = 8;
constexpr int kMaxDevices = 256;

qint64 nowMs()
{
  QElapsedTimer clock;
  clock.start();
  return clock.msecsSinceReference();
}

QString normalizedAddress(const QHostAddress &address)
{
  bool ipv4 = false;
  const auto value = address.toIPv4Address(&ipv4);
  return ipv4 ? QHostAddress(value).toString() : address.toString();
}

bool normalizedManifest(const QJsonArray &manifest, QStringList &normalized, qint64 &totalBytes)
{
  if (!validateTransferManifest(manifest, totalBytes)) {
    return false;
  }
  for (const auto &entry : manifest) {
    const auto file = entry.toObject();
    normalized.append(
        file.value(QStringLiteral("fileName")).toString() + QChar(0) +
        QString::number(file.value(QStringLiteral("size")).toInteger())
    );
  }
  std::sort(normalized.begin(), normalized.end());
  return true;
}
QUrl endpointUrl(const QUrl &base, const QString &endpoint)
{
  QUrl result(base);
  result.setPath(QStringLiteral("/api/localsend/v2/") + endpoint);
  return result;
}

QNetworkRequest jsonRequest(const QUrl &url, int timeoutMs = 10000)
{
  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
  request.setTransferTimeout(timeoutMs);
  return request;
}

bool replySucceeded(QNetworkReply *reply)
{
  return reply->error() == QNetworkReply::NoError &&
         reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
}

} // namespace

LocalSendService::LocalSendService(QObject *parent)
    : QObject(parent),
      m_tcpServer{new QTcpServer(this)},
      m_udpSocket{new QUdpSocket(this)},
      m_networkManager{new QNetworkAccessManager(this)},
      m_announceTimer{new QTimer(this)},
      m_cleanupTimer{new QTimer(this)}
{
  const auto downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
  m_downloadDir = QDir(downloads.isEmpty() ? QDir::homePath() : downloads).filePath(QStringLiteral("FlowPlus"));
  m_fingerprint = QUuid::createUuid().toString(QUuid::WithoutBraces);
  m_tcpServer->setMaxPendingConnections(kMaxConnections);
  connect(m_tcpServer, &QTcpServer::newConnection, this, &LocalSendService::onNewTcpConnection);
  connect(m_udpSocket, &QUdpSocket::readyRead, this, &LocalSendService::onUdpReadyRead);
  connect(m_announceTimer, &QTimer::timeout, this, &LocalSendService::sendAnnouncement);
  connect(m_cleanupTimer, &QTimer::timeout, this, &LocalSendService::expireInactiveTransfers);
}

LocalSendService::~LocalSendService()
{
  stop();
}

bool LocalSendService::start(quint16 port)
{
  if (isRunning()) {
    return true;
  }
  if (!QDir().mkpath(m_downloadDir)) {
    qWarning() << "Could not create file transfer download directory:" << m_downloadDir;
    return false;
  }
  if (!m_tcpServer->listen(QHostAddress::Any, port) && !m_tcpServer->listen(QHostAddress::Any, 0)) {
    qWarning() << "Could not start LocalSend HTTP server:" << m_tcpServer->errorString();
    return false;
  }
  if (m_udpSocket->bind(
          QHostAddress::AnyIPv4, kLocalSendDefaultPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint
      )) {
    m_udpSocket->joinMulticastGroup(QHostAddress(QString::fromLatin1(kLocalSendMulticastGroup)));
  }
  m_announceTimer->start(15000);
  m_cleanupTimer->start(1000);
  QTimer::singleShot(500, this, &LocalSendService::sendAnnouncement);
  return true;
}

void LocalSendService::stop()
{
  ++m_generation;
  m_announceTimer->stop();
  m_cleanupTimer->stop();
  m_tcpServer->close();
  m_udpSocket->close();
  m_grants.clear();
  const auto sessions = m_sessions.keys();
  for (const auto &sessionId : sessions) {
    abortIncomingSession(sessionId, {});
  }
  // Disconnect callbacks before aborting: abort() may emit finished synchronously.
  const auto replies = m_activeReplies.values();
  m_activeReplies.clear();
  m_outgoingTransfers.clear();
  for (auto *reply : replies) {
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
  }
  const auto clients = m_httpClients.keys();
  for (auto *socket : clients) {
    disconnect(socket, nullptr, this, nullptr);
    cleanupClient(socket);
    socket->abort();
    socket->deleteLater();
  }
  m_devices.clear();
  m_deviceLastSeen.clear();
  m_lastRegistration.clear();
}

QString LocalSendService::downloadDirectory() const
{
  return m_downloadDir;
}

void LocalSendService::setDownloadDirectory(const QString &dirPath)
{
  if (!dirPath.isEmpty() && m_sessions.isEmpty()) {
    m_downloadDir = QDir(dirPath).absolutePath();
  }
}

void LocalSendService::setAlias(const QString &alias)
{
  m_deviceAlias = alias.left(128);
}

bool LocalSendService::authorizeIncomingTransfer(
    const QString &peerAddress, const QString &transferId, const QJsonArray &manifest, const QPoint &dropPos
)
{
  const QHostAddress address(peerAddress);
  QStringList normalized;
  qint64 total = 0;
  if (!isRunning() || address.isNull() || address.isMulticast() || address == QHostAddress::Any ||
      address == QHostAddress::AnyIPv4 || address == QHostAddress::AnyIPv6 || !isValidTransferId(transferId) ||
      m_grants.size() >= kMaxSessions || !normalizedManifest(manifest, normalized, total)) {
    return false;
  }
  for (const auto &session : std::as_const(m_sessions)) {
    if (session.transferId == transferId) {
      return false;
    }
  }
  m_grants.insert(
      transferId, IncomingGrant{normalizedAddress(address), normalized, dropPos, nowMs() + kGrantLifetimeMs}
  );
  return true;
}

void LocalSendService::clearIncomingAuthorization(const QString &transferId)
{
  m_grants.remove(transferId);
  const auto sessions = m_sessions.keys();
  for (const auto &sessionId : sessions) {
    if (m_sessions.value(sessionId).transferId == transferId) {
      abortIncomingSession(sessionId, {});
    }
  }
}

void LocalSendService::cancelOutgoingTransfer(const QString &transferId)
{
  const auto transfer = m_outgoingTransfers.take(transferId);
  if (!transfer) {
    return;
  }
  const auto replies = m_activeReplies.values();
  for (auto *reply : replies) {
    if (reply->property("flowplusTransferId").toString() == transferId) {
      m_activeReplies.remove(reply);
      disconnect(reply, nullptr, this, nullptr);
      reply->abort();
      reply->deleteLater();
    }
  }
  cancelRemoteSession(transfer);
}

QJsonObject LocalSendService::buildRegisterDto() const
{
  auto alias = m_deviceAlias;
  if (alias.isEmpty()) {
    alias = QSysInfo::machineHostName();
  }
  return {
      {QStringLiteral("alias"), alias},
      {QStringLiteral("version"), QString::fromLatin1(kLocalSendProtocolVersion)},
      {QStringLiteral("deviceModel"), QSysInfo::prettyProductName()},
      {QStringLiteral("deviceType"), QStringLiteral("desktop")},
      {QStringLiteral("fingerprint"), m_fingerprint},
      {QStringLiteral("port"), static_cast<int>(serverPort())},
      {QStringLiteral("protocol"), QStringLiteral("http")},
      {QStringLiteral("download"), false},
      {QStringLiteral("announce"), true}
  };
}

void LocalSendService::sendAnnouncement()
{
  if (isRunning()) {
    m_udpSocket->writeDatagram(
        QJsonDocument(buildRegisterDto()).toJson(QJsonDocument::Compact),
        QHostAddress(QString::fromLatin1(kLocalSendMulticastGroup)), kLocalSendDefaultPort
    );
  }
}

bool LocalSendService::rememberDevice(const QJsonObject &dto, const QHostAddress &address)
{
  const auto fingerprint = dto.value(QStringLiteral("fingerprint")).toString();
  const auto alias = dto.value(QStringLiteral("alias")).toString();
  const auto version = dto.value(QStringLiteral("version")).toString();
  const auto protocol = dto.value(QStringLiteral("protocol")).toString();
  const int port = dto.value(QStringLiteral("port")).toInt(-1);
  const auto ipAddress = normalizedAddress(address);
  if (fingerprint.isEmpty() || fingerprint.size() > 256 || fingerprint == m_fingerprint || alias.size() > 128 ||
      !version.startsWith(QStringLiteral("2.")) || version.size() > 16 || port < 1 || port > 65535 ||
      (protocol != QStringLiteral("http") && protocol != QStringLiteral("https")) || address.isNull() ||
      address.isMulticast() || (!m_devices.contains(ipAddress) && m_devices.size() >= kMaxDevices)) {
    return false;
  }
  LocalSendDevice device;
  device.alias = alias;
  device.version = version;
  device.deviceModel = dto.value(QStringLiteral("deviceModel")).toString().left(256);
  device.deviceType = dto.value(QStringLiteral("deviceType")).toString(QStringLiteral("desktop")).left(32);
  device.fingerprint = fingerprint;
  device.ipAddress = ipAddress;
  device.port = static_cast<quint16>(port);
  device.protocol = protocol;
  m_devices.insert(ipAddress, device);
  m_deviceLastSeen.insert(ipAddress, nowMs());
  Q_EMIT deviceDiscovered(device);
  return true;
}

void LocalSendService::onUdpReadyRead()
{
  while (m_udpSocket->hasPendingDatagrams()) {
    const bool oversized = m_udpSocket->pendingDatagramSize() > kMaxHeaderBytes;
    const auto datagram = m_udpSocket->receiveDatagram(kMaxHeaderBytes);
    const auto doc = QJsonDocument::fromJson(datagram.data());
    if (oversized || !doc.isObject() || !rememberDevice(doc.object(), datagram.senderAddress())) {
      continue;
    }
    const auto device = m_devices.value(normalizedAddress(datagram.senderAddress()));
    const auto now = nowMs();
    // HTTPS discovery remains visible, but we never bypass certificate validation
    // or silently downgrade an HTTPS peer to HTTP.
    if (device.protocol == QStringLiteral("http") && doc.object().value(QStringLiteral("announce")).toBool() &&
        now - m_lastRegistration.value(device.ipAddress, -15000) >= 15000 && m_activeReplies.size() < kMaxDevices) {
      m_lastRegistration.insert(device.ipAddress, now);
      QUrl base;
      base.setScheme(device.protocol);
      base.setHost(device.ipAddress);
      base.setPort(device.port);
      auto *reply = m_networkManager->post(
          jsonRequest(endpointUrl(base, QStringLiteral("register"))),
          QJsonDocument(buildRegisterDto()).toJson(QJsonDocument::Compact)
      );
      trackReply(reply, 10000);
      connect(reply, &QNetworkReply::readyRead, this, [reply]() { reply->readAll(); });
      connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    }
  }
}

void LocalSendService::onNewTcpConnection()
{
  while (m_tcpServer->hasPendingConnections()) {
    auto *socket = m_tcpServer->nextPendingConnection();
    if (!socket) {
      continue;
    }
    if (m_httpClients.size() >= kMaxConnections) {
      socket->abort();
      socket->deleteLater();
      continue;
    }
    socket->setReadBufferSize(kChunkBytes);
    HttpClientState state;
    state.connectedAt = state.lastActivity = nowMs();
    m_httpClients.insert(socket, state);
    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { handleClientSocketData(socket); });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
      cleanupClient(socket);
      socket->deleteLater();
    });
    if (socket->bytesAvailable() > 0) {
      handleClientSocketData(socket);
    }
  }
}

bool LocalSendService::parseHeaders(QTcpSocket *socket)
{
  auto &state = m_httpClients[socket];
  const auto headerEnd = state.buffer.indexOf("\r\n\r\n");
  if (headerEnd < 0) {
    if (state.buffer.size() > kMaxHeaderBytes) {
      sendEmptyResponse(socket, 431, "Request Header Fields Too Large");
    }
    return false;
  }
  if (headerEnd > kMaxHeaderBytes) {
    sendEmptyResponse(socket, 431, "Request Header Fields Too Large");
    return false;
  }
  const auto lines = state.buffer.left(headerEnd).split('\n');
  state.buffer.remove(0, headerEnd + 4);
  const auto requestLine = lines.first().trimmed().split(' ');
  if (requestLine.size() != 3 || !requestLine.at(1).startsWith('/') ||
      (requestLine.at(2) != "HTTP/1.1" && requestLine.at(2) != "HTTP/1.0")) {
    sendEmptyResponse(socket, 400, "Bad Request");
    return false;
  }
  const auto url = QUrl::fromEncoded(requestLine.at(1), QUrl::StrictMode);
  if (!url.isValid() || !url.host().isEmpty() || !url.scheme().isEmpty() || url.hasFragment()) {
    sendEmptyResponse(socket, 400, "Bad Request");
    return false;
  }
  state.method = QString::fromLatin1(requestLine.at(0));
  state.path = url.path();
  for (const auto &pair : QUrlQuery(url).queryItems(QUrl::FullyDecoded)) {
    if (state.query.contains(pair.first)) {
      sendEmptyResponse(socket, 400, "Duplicate Query Parameter");
      return false;
    }
    state.query.insert(pair.first, pair.second);
  }
  bool haveLength = false;
  bool expectContinue = false;
  for (qsizetype i = 1; i < lines.size(); ++i) {
    const auto line = lines.at(i).trimmed();
    const auto colon = line.indexOf(':');
    if (colon <= 0) {
      sendEmptyResponse(socket, 400, "Bad Request");
      return false;
    }
    const auto key = line.left(colon).toLower();
    const auto value = line.mid(colon + 1).trimmed();
    if (key == "transfer-encoding") {
      sendEmptyResponse(socket, 501, "Transfer Encoding Not Supported");
      return false;
    }
    if (key == "content-length") {
      bool valid = false;
      const auto length = value.toLongLong(&valid);
      for (const auto ch : value) {
        valid = valid && ch >= '0' && ch <= '9';
      }
      if (haveLength || !valid || length < 0 || length > kMaxTransferBytes) {
        sendEmptyResponse(socket, 400, "Invalid Content Length");
        return false;
      }
      haveLength = true;
      state.contentLength = length;
    }
    if (key == "expect") {
      if (value.toLower() != "100-continue") {
        sendEmptyResponse(socket, 417, "Expectation Failed");
        return false;
      }
      expectContinue = true;
    }
  }
  if (state.method != QStringLiteral("POST") && state.method != QStringLiteral("GET")) {
    sendEmptyResponse(socket, 405, "Method Not Allowed");
    return false;
  }
  if (state.method == QStringLiteral("POST") && !haveLength) {
    sendEmptyResponse(socket, 411, "Length Required");
    return false;
  }
  state.headersParsed = true;
  if (state.method == QStringLiteral("POST") && state.path == QStringLiteral("/api/localsend/v2/upload")) {
    state.sessionId = state.query.value(QStringLiteral("sessionId"));
    state.fileId = state.query.value(QStringLiteral("fileId"));
    auto sessionIt = m_sessions.find(state.sessionId);
    if (sessionIt == m_sessions.end() || sessionIt->peerAddress != normalizedAddress(socket->peerAddress()) ||
        !sessionIt->files.contains(state.fileId)) {
      sendEmptyResponse(socket, 403, "Forbidden");
      return false;
    }
    auto &file = sessionIt->files[state.fileId];
    if (file.token.isEmpty() || file.token != state.query.value(QStringLiteral("token")) || file.completed ||
        file.uploading) {
      sendEmptyResponse(socket, 403, "Forbidden");
      return false;
    }
    if (state.contentLength != file.size) {
      sendEmptyResponse(socket, 400, "File Size Mismatch");
      return false;
    }
    state.outputFile = new QTemporaryFile(QDir(m_downloadDir).filePath(QStringLiteral(".flowplus-XXXXXX.part")));
    if (!state.outputFile->open()) {
      delete state.outputFile;
      state.outputFile = nullptr;
      sendEmptyResponse(socket, 500, "Could Not Create File");
      return false;
    }
    state.isStreamingUpload = true;
    file.uploading = true;
    sessionIt->lastActivity = nowMs();
  } else if (state.contentLength > kMaxJsonBytes) {
    sendEmptyResponse(socket, 413, "Content Too Large");
    return false;
  }
  if (expectContinue) {
    socket->write("HTTP/1.1 100 Continue\r\n\r\n");
  }
  return true;
}

void LocalSendService::handleClientSocketData(QTcpSocket *socket)
{
  while (m_httpClients.contains(socket) && !m_httpClients[socket].responseSent) {
    auto &state = m_httpClients[socket];
    state.lastActivity = nowMs();
    state.buffer.append(socket->read(kChunkBytes));
    if (!state.headersParsed && !parseHeaders(socket)) {
      return;
    }
    if (state.isStreamingUpload) {
      receiveUpload(socket);
    } else if (state.buffer.size() >= state.contentLength) {
      if (state.buffer.size() != state.contentLength) {
        sendEmptyResponse(socket, 400, "Unexpected Request Data");
      } else {
        handleCompleteHttpRequest(socket, state.buffer);
      }
    }
    if (socket->bytesAvailable() == 0) {
      return;
    }
  }
}

void LocalSendService::receiveUpload(QTcpSocket *socket)
{
  auto &state = m_httpClients[socket];
  const auto sessionId = state.sessionId;
  if (!state.outputFile || !m_sessions.contains(sessionId)) {
    sendEmptyResponse(socket, 403, "Forbidden");
    return;
  }
  const auto remaining = state.contentLength - state.bodyBytesReceived;
  if (state.buffer.size() > remaining) {
    sendEmptyResponse(socket, 400, "Unexpected Request Data");
    abortIncomingSession(sessionId, tr("Received more bytes than declared"));
    return;
  }
  if (!state.buffer.isEmpty()) {
    const qint64 count = state.buffer.size();
    if (state.outputFile->write(state.buffer) != count) {
      sendEmptyResponse(socket, 507, "Insufficient Storage");
      abortIncomingSession(sessionId, tr("Could not save incoming file"));
      return;
    }
    state.bodyBytesReceived += count;
    state.buffer.clear();
    m_sessions[sessionId].lastActivity = nowMs();
    const auto file = m_sessions[sessionId].files.value(state.fileId);
    Q_EMIT transferProgress(file.fileName, state.bodyBytesReceived, file.size);
    if (!m_httpClients.contains(socket) || !m_sessions.contains(sessionId)) {
      return;
    }
  }
  if (state.bodyBytesReceived == state.contentLength) {
    finishIncomingFile(socket);
  }
}

void LocalSendService::finishIncomingFile(QTcpSocket *socket)
{
  auto &state = m_httpClients[socket];
  const auto sessionId = state.sessionId;
  if (!state.outputFile->flush()) {
    sendEmptyResponse(socket, 507, "Insufficient Storage");
    abortIncomingSession(sessionId, tr("Could not save incoming file"));
    return;
  }
  state.outputFile->close();
  auto &file = m_sessions[sessionId].files[state.fileId];
  file.stagingPath = state.outputFile->fileName();
  state.outputFile->setAutoRemove(false);
  delete state.outputFile;
  state.outputFile = nullptr;
  state.isStreamingUpload = false;
  file.completed = true;
  file.uploading = false;
  for (const auto &entry : std::as_const(m_sessions[sessionId].files)) {
    if (!entry.completed) {
      sendEmptyResponse(socket, 200, "OK");
      return;
    }
  }
  const auto session = m_sessions.value(sessionId);
  QStringList savedPaths;
  for (const auto &fileId : session.fileIds) {
    const auto pending = session.files.value(fileId);
    bool saved = false;
    for (int attempts = 0; attempts < 10 && !saved; ++attempts) {
      const auto destination = uniqueTransferDestination(m_downloadDir, pending.fileName);
      if (destination.isEmpty()) {
        break;
      }
      // QFile::rename refuses to replace an existing destination; retry a race.
      if (QFile::rename(pending.stagingPath, destination)) {
        savedPaths.append(destination);
        saved = true;
      } else if (!QFileInfo::exists(destination)) {
        break;
      }
    }
    if (!saved) {
      for (const auto &path : savedPaths) {
        QFile::remove(path);
      }
      sendEmptyResponse(socket, 500, "Could Not Publish Files");
      abortIncomingSession(sessionId, tr("Could not publish received files"));
      return;
    }
  }
  m_sessions.remove(sessionId);
  sendEmptyResponse(socket, 200, "OK");
  for (const auto &path : savedPaths) {
    Q_EMIT fileReceived(path, session.dropPos);
  }
  Q_EMIT transferCompleted(savedPaths, session.dropPos, session.transferId);
}

void LocalSendService::handleCompleteHttpRequest(QTcpSocket *socket, const QByteArray &body)
{
  // Copy state before emitting signals or responding; callbacks may revoke a session.
  const auto state = m_httpClients.value(socket);
  if (state.method == QStringLiteral("GET") && state.path == QStringLiteral("/api/localsend/v2/info")) {
    sendJsonResponse(socket, 200, "OK", QJsonDocument(buildRegisterDto()).toJson(QJsonDocument::Compact));
    return;
  }
  if (state.method == QStringLiteral("POST") && state.path == QStringLiteral("/api/localsend/v2/register")) {
    const auto doc = QJsonDocument::fromJson(body);
    if (!doc.isObject() || !rememberDevice(doc.object(), socket->peerAddress())) {
      sendEmptyResponse(socket, 400, "Invalid Device Information");
      return;
    }
    sendJsonResponse(socket, 200, "OK", QJsonDocument(buildRegisterDto()).toJson(QJsonDocument::Compact));
    return;
  }
  if (state.method == QStringLiteral("POST") && state.path == QStringLiteral("/api/localsend/v2/prepare-upload")) {
    const auto doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
      sendEmptyResponse(socket, 400, "Invalid JSON");
      return;
    }
    const auto root = doc.object();
    const auto transferId = root.value(QStringLiteral("flowPlusTransferId")).toString();
    const auto grantIt = m_grants.constFind(transferId);
    const auto address = normalizedAddress(socket->peerAddress());
    if (grantIt == m_grants.cend() || grantIt->expiresAt <= nowMs() || grantIt->peerAddress != address) {
      sendEmptyResponse(socket, 403, "Transfer Not Authorized");
      return;
    }
    if (m_sessions.size() >= kMaxSessions) {
      sendEmptyResponse(socket, 503, "Too Many Transfers");
      return;
    }
    const auto files = root.value(QStringLiteral("files")).toObject();
    QJsonArray manifest;
    for (auto it = files.begin(); it != files.end(); ++it) {
      if (it.key().isEmpty() || it.key().size() > 128 || !it.value().isObject()) {
        sendEmptyResponse(socket, 400, "Invalid File Metadata");
        return;
      }
      const auto id = it.value().toObject().value(QStringLiteral("id")).toString();
      if (!id.isEmpty() && id != it.key()) {
        sendEmptyResponse(socket, 400, "Invalid File ID");
        return;
      }
      manifest.append(it.value());
    }
    QStringList normalized;
    qint64 total = 0;
    if (!normalizedManifest(manifest, normalized, total) || normalized != grantIt->manifest) {
      sendEmptyResponse(socket, 403, "Manifest Does Not Match Authorized Transfer");
      return;
    }
    if (!QDir().mkpath(m_downloadDir)) {
      sendEmptyResponse(socket, 507, "Insufficient Storage");
      return;
    }
    const QStorageInfo storage(m_downloadDir);
    if (storage.isValid() && storage.isReady() && (storage.isReadOnly() || total > storage.bytesAvailable())) {
      sendEmptyResponse(socket, 507, "Insufficient Storage");
      return;
    }
    UploadSession session;
    session.transferId = transferId;
    session.peerAddress = address;
    session.dropPos = grantIt->dropPos;
    session.lastActivity = nowMs();
    QJsonObject responseFiles;
    QStringList fileNames;
    for (auto it = files.begin(); it != files.end(); ++it) {
      const auto dto = it.value().toObject();
      IncomingFileSession file;
      file.fileName = dto.value(QStringLiteral("fileName")).toString();
      file.size = dto.value(QStringLiteral("size")).toInteger();
      file.token = QUuid::createUuid().toString(QUuid::WithoutBraces);
      responseFiles.insert(it.key(), file.token);
      fileNames.append(file.fileName);
      session.fileIds.append(it.key());
      session.files.insert(it.key(), file);
    }
    const auto sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_grants.remove(transferId);
    m_sessions.insert(sessionId, session);
    const QJsonObject response{{QStringLiteral("sessionId"), sessionId}, {QStringLiteral("files"), responseFiles}};
    sendJsonResponse(socket, 200, "OK", QJsonDocument(response).toJson(QJsonDocument::Compact));
    Q_EMIT transferStarted(sessionId, fileNames, total);
    return;
  }
  if (state.method == QStringLiteral("POST") && state.path == QStringLiteral("/api/localsend/v2/cancel")) {
    const auto sessionId = state.query.value(QStringLiteral("sessionId"));
    if (!m_sessions.contains(sessionId) ||
        m_sessions.value(sessionId).peerAddress != normalizedAddress(socket->peerAddress())) {
      sendEmptyResponse(socket, 403, "Forbidden");
      return;
    }
    abortIncomingSession(sessionId, tr("Incoming transfer cancelled by sender"));
    sendEmptyResponse(socket, 200, "OK");
    return;
  }
  sendEmptyResponse(socket, 404, "Not Found");
}

void LocalSendService::cleanupClient(QTcpSocket *socket)
{
  const auto state = m_httpClients.take(socket);
  if (state.outputFile) {
    delete state.outputFile;
    abortIncomingSession(state.sessionId, tr("Incoming file upload was interrupted"));
  }
}

void LocalSendService::abortIncomingSession(const QString &sessionId, const QString &reason)
{
  if (!m_sessions.contains(sessionId)) {
    return;
  }
  const auto session = m_sessions.take(sessionId);
  for (const auto &file : session.files) {
    if (!file.stagingPath.isEmpty()) {
      QFile::remove(file.stagingPath);
    }
  }
  const auto clients = m_httpClients.keys();
  for (auto *socket : clients) {
    if (m_httpClients.value(socket).sessionId == sessionId) {
      auto &state = m_httpClients[socket];
      delete state.outputFile;
      state.outputFile = nullptr;
      state.isStreamingUpload = false;
      if (!state.responseSent) {
        sendEmptyResponse(socket, 410, "Transfer Cancelled");
      }
    }
  }
  if (!reason.isEmpty()) {
    Q_EMIT incomingTransferFailed(reason, session.transferId);
  }
}

void LocalSendService::expireInactiveTransfers()
{
  const auto now = nowMs();
  for (auto it = m_grants.begin(); it != m_grants.end();) {
    if (it->expiresAt <= now) {
      it = m_grants.erase(it);
    } else {
      ++it;
    }
  }
  const auto clients = m_httpClients.keys();
  for (auto *socket : clients) {
    const auto state = m_httpClients.value(socket);
    if (now - state.lastActivity > kIdleTimeoutMs || (!state.headersParsed && now - state.connectedAt > 15000) ||
        (!state.isStreamingUpload && now - state.connectedAt > kIdleTimeoutMs)) {
      cleanupClient(socket);
      socket->abort();
    }
  }
  const auto sessions = m_sessions.keys();
  for (const auto &sessionId : sessions) {
    if (now - m_sessions.value(sessionId).lastActivity > kGrantLifetimeMs) {
      abortIncomingSession(sessionId, tr("Incoming transfer timed out"));
    }
  }
  for (auto it = m_deviceLastSeen.begin(); it != m_deviceLastSeen.end();) {
    if (now - it.value() > kIdleTimeoutMs) {
      m_devices.remove(it.key());
      m_lastRegistration.remove(it.key());
      it = m_deviceLastSeen.erase(it);
    } else {
      ++it;
    }
  }
}

void LocalSendService::sendJsonResponse(
    QTcpSocket *socket, int statusCode, const QByteArray &statusText, const QByteArray &jsonBody
)
{
  auto it = m_httpClients.find(socket);
  if (it == m_httpClients.end() || it->responseSent) {
    return;
  }
  it->responseSent = true;
  it->buffer.clear();
  QByteArray response = "HTTP/1.1 " + QByteArray::number(statusCode) + " " + statusText + "\r\n";
  response += "Content-Type: application/json; charset=utf-8\r\n";
  response += "Content-Length: " + QByteArray::number(jsonBody.size()) + "\r\n";
  response += "Connection: close\r\n\r\n";
  response += jsonBody;
  socket->write(response);
  socket->disconnectFromHost();
}

void LocalSendService::sendEmptyResponse(QTcpSocket *socket, int statusCode, const QByteArray &statusText)
{
  sendJsonResponse(socket, statusCode, statusText);
}

void LocalSendService::trackReply(QNetworkReply *reply, int hardTimeoutMs)
{
  reply->setReadBufferSize(kChunkBytes);
  m_activeReplies.insert(reply);
  connect(reply, &QNetworkReply::finished, this, [this, reply]() { m_activeReplies.remove(reply); });
  if (hardTimeoutMs > 0) {
    QTimer::singleShot(hardTimeoutMs, reply, [reply]() {
      if (reply->isRunning()) {
        reply->abort();
      }
    });
  }
}

void LocalSendService::sendFiles(
    const QString &targetHost, quint16 port, const QStringList &filePaths, const QPoint &dropPos,
    const QString &transferId
)
{
  const auto id = transferId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : transferId;
  auto transfer = std::make_shared<OutgoingTransfer>();
  transfer->transferId = id;
  transfer->dropPos = dropPos;
  transfer->generation = m_generation;
  transfer->baseUrl.setScheme(QStringLiteral("http"));
  transfer->baseUrl.setHost(targetHost);
  transfer->baseUrl.setPort(port);
  transfer->paths = filePaths;
  if (targetHost.isEmpty() || port == 0 || !transfer->baseUrl.isValid() || filePaths.isEmpty() ||
      filePaths.size() > kMaxTransferFiles || !isValidTransferId(id) || m_outgoingTransfers.contains(id) ||
      m_outgoingTransfers.size() >= kMaxSessions) {
    Q_EMIT transferFailed(tr("Invalid file transfer request"), filePaths, dropPos, false, id);
    return;
  }
  // Discovery data is untrusted, but never reinterpret an HTTPS advertisement
  // as permission to send unencrypted HTTP to the same device.
  const auto peer = m_devices.constFind(normalizedAddress(QHostAddress(targetHost)));
  if (peer != m_devices.cend() && peer->port == port && peer->protocol == QStringLiteral("https")) {
    Q_EMIT transferFailed(
        tr("HTTPS LocalSend peers are not supported by this transport"), filePaths, dropPos, false, id
    );
    return;
  }
  QJsonObject filesJson;
  QMimeDatabase mimeDatabase;
  qint64 total = 0;
  for (int i = 0; i < filePaths.size(); ++i) {
    const QFileInfo info(filePaths.at(i));
    const auto size = info.size();
    if (!info.isFile() || !info.isReadable() || !isPortableFileName(info.fileName()) || size < 0 ||
        size > kMaxTransferBytes - total) {
      Q_EMIT transferFailed(tr("File cannot be transferred: %1").arg(filePaths.at(i)), filePaths, dropPos, false, id);
      return;
    }
    total += size;
    const auto fileId = QStringLiteral("file_%1").arg(i);
    const QJsonObject fileDto{
        {QStringLiteral("id"), fileId},
        {QStringLiteral("fileName"), info.fileName()},
        {QStringLiteral("size"), size},
        {QStringLiteral("fileType"), mimeDatabase.mimeTypeForFile(info, QMimeDatabase::MatchExtension).name()}
    };
    filesJson.insert(fileId, fileDto);
    transfer->fileIds.append(fileId);
    transfer->pathsById.insert(fileId, info.absoluteFilePath());
    transfer->sizesById.insert(fileId, size);
  }
  m_outgoingTransfers.insert(id, transfer);
  const QJsonObject payload{
      {QStringLiteral("info"), buildRegisterDto()},
      {QStringLiteral("files"), filesJson},
      {QStringLiteral("flowPlusTransferId"), id}
  };
  auto *reply = m_networkManager->post(
      jsonRequest(endpointUrl(transfer->baseUrl, QStringLiteral("prepare-upload"))),
      QJsonDocument(payload).toJson(QJsonDocument::Compact)
  );
  trackReply(reply, 10000);
  reply->setProperty("flowplusTransferId", id);
  const auto responseBody = std::make_shared<QByteArray>();
  connect(reply, &QNetworkReply::readyRead, this, [reply, responseBody]() {
    responseBody->append(reply->readAll());
    if (responseBody->size() > kMaxJsonBytes) {
      reply->setProperty("flowplusOversizedResponse", true);
      reply->abort();
    }
  });
  connect(reply, &QNetworkReply::finished, this, [this, reply, responseBody, transfer]() {
    reply->deleteLater();
    if (transfer->generation != m_generation || !m_outgoingTransfers.contains(transfer->transferId)) {
      return;
    }
    if (!replySucceeded(reply)) {
      const bool allowFallback = !reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).isValid() &&
                                 !reply->property("flowplusOversizedResponse").toBool();
      failOutgoingTransfer(
          transfer, tr("Receiver did not accept the transfer: %1").arg(reply->errorString()), 0, allowFallback
      );
      return;
    }
    responseBody->append(reply->readAll());
    const auto response = QJsonDocument::fromJson(*responseBody);
    const auto sessionId = response.object().value(QStringLiteral("sessionId")).toString();
    const auto tokens = response.object().value(QStringLiteral("files")).toObject();
    if (!sessionId.isEmpty() && sessionId.size() <= 128) {
      transfer->sessionId = sessionId;
    }
    if (responseBody->size() > kMaxJsonBytes || !response.isObject() || sessionId.isEmpty() || sessionId.size() > 128 ||
        tokens.size() != transfer->fileIds.size()) {
      failOutgoingTransfer(transfer, tr("Receiver returned an invalid or partial acceptance"), 0, false);
      return;
    }
    transfer->sessionId = sessionId;
    for (const auto &fileId : transfer->fileIds) {
      const auto token = tokens.value(fileId).toString();
      if (token.isEmpty() || token.size() > 256) {
        failOutgoingTransfer(transfer, tr("Receiver returned an invalid upload token"), 0, false);
        return;
      }
      transfer->tokens.insert(fileId, token);
    }
    uploadNextFile(transfer, 0);
  });
}

void LocalSendService::uploadNextFile(const std::shared_ptr<OutgoingTransfer> &transfer, int index)
{
  if (transfer->generation != m_generation || !m_outgoingTransfers.contains(transfer->transferId)) {
    return;
  }
  if (index >= transfer->fileIds.size()) {
    m_outgoingTransfers.remove(transfer->transferId);
    Q_EMIT filesSent(transfer->paths, transfer->dropPos, transfer->transferId);
    return;
  }
  const auto fileId = transfer->fileIds.at(index);
  const auto filePath = transfer->pathsById.value(fileId);
  auto *file = new QFile(filePath);
  if (!file->open(QIODevice::ReadOnly) || file->size() != transfer->sizesById.value(fileId)) {
    delete file;
    failOutgoingTransfer(transfer, tr("File changed or cannot be opened: %1").arg(filePath), index, false);
    return;
  }
  auto url = endpointUrl(transfer->baseUrl, QStringLiteral("upload"));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("sessionId"), transfer->sessionId);
  query.addQueryItem(QStringLiteral("fileId"), fileId);
  query.addQueryItem(QStringLiteral("token"), transfer->tokens.value(fileId));
  url.setQuery(query);
  auto request = jsonRequest(url, static_cast<int>(kIdleTimeoutMs));
  request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
  request.setHeader(QNetworkRequest::ContentLengthHeader, file->size());
  auto *reply = m_networkManager->post(request, file);
  file->setParent(reply);
  trackReply(reply);
  reply->setProperty("flowplusTransferId", transfer->transferId);
  const auto name = QFileInfo(filePath).fileName();
  const auto size = file->size();
  connect(reply, &QNetworkReply::uploadProgress, this, [this, name, size](qint64 sent, qint64) {
    Q_EMIT transferProgress(name, qBound<qint64>(0, sent, size), size);
  });
  const auto responseBytes = std::make_shared<qint64>(0);
  connect(reply, &QNetworkReply::readyRead, this, [reply, responseBytes]() {
    *responseBytes += reply->readAll().size();
    if (*responseBytes > kMaxJsonBytes) {
      reply->abort();
    }
  });
  connect(reply, &QNetworkReply::finished, this, [this, reply, transfer, index]() {
    reply->deleteLater();
    if (transfer->generation != m_generation || !m_outgoingTransfers.contains(transfer->transferId)) {
      return;
    }
    if (!replySucceeded(reply)) {
      // A missing acknowledgement may follow a successful write. Re-sending through
      // another transport at this point can duplicate files and is never automatic.
      failOutgoingTransfer(transfer, tr("File upload failed: %1").arg(reply->errorString()), index, false);
      return;
    }
    uploadNextFile(transfer, index + 1);
  });
}

void LocalSendService::cancelRemoteSession(const std::shared_ptr<OutgoingTransfer> &transfer)
{
  if (transfer->sessionId.isEmpty()) {
    return;
  }
  auto url = endpointUrl(transfer->baseUrl, QStringLiteral("cancel"));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("sessionId"), transfer->sessionId);
  url.setQuery(query);
  auto *reply = m_networkManager->post(jsonRequest(url), QByteArray());
  trackReply(reply, 10000);
  connect(reply, &QNetworkReply::readyRead, this, [reply]() { reply->readAll(); });
  connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
}

void LocalSendService::failOutgoingTransfer(
    const std::shared_ptr<OutgoingTransfer> &transfer, const QString &reason, int nextIndex, bool allowFallback
)
{
  cancelRemoteSession(transfer);
  m_outgoingTransfers.remove(transfer->transferId);
  QStringList remaining;
  for (int i = nextIndex; i < transfer->fileIds.size(); ++i) {
    remaining.append(transfer->pathsById.value(transfer->fileIds.at(i)));
  }
  Q_EMIT transferFailed(reason, remaining, transfer->dropPos, allowFallback, transfer->transferId);
}

} // namespace deskflow::gui::transfer
