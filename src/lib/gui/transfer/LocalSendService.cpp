/*
 * FlowPlus -- mouse, keyboard, and file sharing utility
 * SPDX-FileCopyrightText: (C) 2026 FlowPlus Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "LocalSendService.h"

#include "common/Constants.h"
#include "common/Settings.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

namespace deskflow::gui::transfer {

LocalSendService::LocalSendService(QObject *parent)
    : QObject(parent),
      m_tcpServer{new QTcpServer(this)},
      m_udpSocket{new QUdpSocket(this)},
      m_networkManager{new QNetworkAccessManager(this)},
      m_announceTimer{new QTimer(this)}
{
  const auto downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
  m_downloadDir = QStringLiteral("%1/%2").arg(downloads.isEmpty() ? QDir::homePath() : downloads, kAppName);
  QDir().mkpath(m_downloadDir);

  const auto seed =
      QStringLiteral("%1-%2-%3")
          .arg(QSysInfo::machineUniqueId(), QSysInfo::machineHostName(), QString::number(QDateTime::currentMSecsSinceEpoch()));
  m_fingerprint = QString::fromLatin1(QCryptographicHash::hash(seed.toUtf8(), QCryptographicHash::Sha256).toHex()).toUpper();

  connect(m_tcpServer, &QTcpServer::newConnection, this, &LocalSendService::onNewTcpConnection);
  connect(m_udpSocket, &QUdpSocket::readyRead, this, &LocalSendService::onUdpReadyRead);
  connect(m_announceTimer, &QTimer::timeout, this, &LocalSendService::sendAnnouncement);
}

LocalSendService::~LocalSendService()
{
  stop();
}

bool LocalSendService::start(quint16 port)
{
  if (m_tcpServer->isListening()) {
    return true;
  }

  if (!m_tcpServer->listen(QHostAddress::Any, port)) {
    // Fallback to any available port if 53317 is already occupied by a standalone LocalSend instance
    if (!m_tcpServer->listen(QHostAddress::Any, 0)) {
      qWarning() << "LocalSendService failed to start TCP server:" << m_tcpServer->errorString();
      return false;
    }
  }

  if (m_udpSocket->bind(
          QHostAddress::AnyIPv4, kLocalSendDefaultPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint
      )) {
    m_udpSocket->joinMulticastGroup(QHostAddress(QString::fromLatin1(kLocalSendMulticastGroup)));
  }

  m_announceTimer->start(15000);
  QTimer::singleShot(500, this, &LocalSendService::sendAnnouncement);

  qInfo().noquote() << QStringLiteral("LocalSend v%1 service listening on port %2 (downloads: %3)")
                           .arg(kLocalSendProtocolVersion)
                           .arg(m_tcpServer->serverPort())
                           .arg(m_downloadDir);
  return true;
}

void LocalSendService::stop()
{
  m_announceTimer->stop();
  for (auto it = m_httpClients.begin(); it != m_httpClients.end(); ++it) {
    if (it.value().outputFile) {
      it.value().outputFile->close();
      delete it.value().outputFile;
    }
    it.key()->close();
  }
  m_httpClients.clear();

  if (m_udpSocket->state() != QAbstractSocket::UnconnectedState) {
    m_udpSocket->close();
  }
  if (m_tcpServer->isListening()) {
    m_tcpServer->close();
  }
}

QString LocalSendService::downloadDirectory() const
{
  return m_downloadDir;
}

void LocalSendService::setDownloadDirectory(const QString &dirPath)
{
  if (!dirPath.isEmpty()) {
    m_downloadDir = dirPath;
    QDir().mkpath(m_downloadDir);
  }
}

QJsonObject LocalSendService::buildRegisterDto() const
{
  QString alias = Settings::value(Settings::Core::ComputerName).toString();
  if (alias.isEmpty()) {
    alias = QSysInfo::machineHostName();
  }

  QJsonObject obj;
  obj[QStringLiteral("alias")] = alias;
  obj[QStringLiteral("version")] = QString::fromLatin1(kLocalSendProtocolVersion);
  obj[QStringLiteral("deviceModel")] = QSysInfo::prettyProductName();
  obj[QStringLiteral("deviceType")] = QStringLiteral("desktop");
  obj[QStringLiteral("fingerprint")] = m_fingerprint;
  obj[QStringLiteral("port")] = static_cast<int>(serverPort());
  obj[QStringLiteral("protocol")] = QStringLiteral("http");
  obj[QStringLiteral("download")] = false;
  obj[QStringLiteral("announce")] = true;
  return obj;
}

void LocalSendService::sendAnnouncement()
{
  if (!isRunning()) {
    return;
  }

  const QByteArray payload = QJsonDocument(buildRegisterDto()).toJson(QJsonDocument::Compact);
  m_udpSocket->writeDatagram(payload, QHostAddress(QString::fromLatin1(kLocalSendMulticastGroup)), kLocalSendDefaultPort);
}

void LocalSendService::onUdpReadyRead()
{
  while (m_udpSocket->hasPendingDatagrams()) {
    const QNetworkDatagram datagram = m_udpSocket->receiveDatagram();
    const QJsonDocument doc = QJsonDocument::fromJson(datagram.data());
    if (!doc.isObject()) {
      continue;
    }

    const QJsonObject obj = doc.object();
    const QString fingerprint = obj.value(QStringLiteral("fingerprint")).toString();
    if (fingerprint.isEmpty() || fingerprint == m_fingerprint) {
      continue;
    }

    LocalSendDevice dev;
    dev.alias = obj.value(QStringLiteral("alias")).toString();
    dev.version = obj.value(QStringLiteral("version")).toString(QString::fromLatin1(kLocalSendProtocolVersion));
    dev.deviceModel = obj.value(QStringLiteral("deviceModel")).toString();
    dev.deviceType = obj.value(QStringLiteral("deviceType")).toString(QStringLiteral("desktop"));
    dev.fingerprint = fingerprint;
    dev.ipAddress = datagram.senderAddress().toString();
    if (dev.ipAddress.startsWith(QStringLiteral("::ffff:"))) {
      dev.ipAddress = dev.ipAddress.mid(7);
    }
    dev.port = static_cast<quint16>(obj.value(QStringLiteral("port")).toInt(kLocalSendDefaultPort));
    dev.protocol = obj.value(QStringLiteral("protocol")).toString(QStringLiteral("http"));

    m_devices.insert(dev.ipAddress, dev);
    Q_EMIT deviceDiscovered(dev);

    if (obj.value(QStringLiteral("announce")).toBool()) {
      const QUrl regUrl(QStringLiteral("http://%1:%2/api/localsend/v2/register").arg(dev.ipAddress).arg(dev.port));
      QNetworkRequest req(regUrl);
      req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
      QNetworkReply *reply = m_networkManager->post(req, QJsonDocument(buildRegisterDto()).toJson(QJsonDocument::Compact));
      connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    }
  }
}

void LocalSendService::onNewTcpConnection()
{
  while (m_tcpServer->hasPendingConnections()) {
    QTcpSocket *socket = m_tcpServer->nextPendingConnection();
    if (!socket) {
      continue;
    }

    m_httpClients.insert(socket, HttpClientState{});
    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { handleClientSocketData(socket); });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
      if (m_httpClients.contains(socket)) {
        auto state = m_httpClients.take(socket);
        if (state.outputFile) {
          state.outputFile->close();
          delete state.outputFile;
        }
      }
      socket->deleteLater();
    });
  }
}

void LocalSendService::handleClientSocketData(QTcpSocket *socket)
{
  if (!m_httpClients.contains(socket)) {
    return;
  }

  HttpClientState &state = m_httpClients[socket];
  state.buffer.append(socket->readAll());

  if (!state.headersParsed) {
    const qsizetype headerEnd = state.buffer.indexOf("\r\n\r\n");
    if (headerEnd == -1) {
      return;
    }

    const QByteArray headerBlock = state.buffer.left(headerEnd);
    state.buffer.remove(0, headerEnd + 4);

    const QList<QByteArray> lines = headerBlock.split('\n');
    if (lines.isEmpty()) {
      sendEmptyResponse(socket, 400, "Bad Request");
      return;
    }

    const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
    if (requestLine.size() < 2) {
      sendEmptyResponse(socket, 400, "Bad Request");
      return;
    }

    state.method = QString::fromLatin1(requestLine.at(0));
    const QUrl fullUrl(QString::fromUtf8(requestLine.at(1)));
    state.path = fullUrl.path();
    const QUrlQuery urlQuery(fullUrl);
    for (const auto &pair : urlQuery.queryItems(QUrl::FullyDecoded)) {
      state.query.insert(pair.first, pair.second);
    }

    for (qsizetype i = 1; i < lines.size(); ++i) {
      const QByteArray line = lines.at(i).trimmed();
      const qsizetype colon = line.indexOf(':');
      if (colon > 0) {
        const QByteArray key = line.left(colon).trimmed().toLower();
        const QByteArray val = line.mid(colon + 1).trimmed();
        if (key == "content-length") {
          state.contentLength = val.toLongLong();
        }
      }
    }

    state.headersParsed = true;

    if (state.method == QStringLiteral("POST") && state.path == QStringLiteral("/api/localsend/v2/upload")) {
      state.sessionId = state.query.value(QStringLiteral("sessionId"));
      state.fileId = state.query.value(QStringLiteral("fileId"));
      state.token = state.query.value(QStringLiteral("token"));

      if (!m_sessions.contains(state.sessionId) || !m_sessions[state.sessionId].files.contains(state.fileId)) {
        sendEmptyResponse(socket, 403, "Forbidden");
        return;
      }

      IncomingFileSession &fileSession = m_sessions[state.sessionId].files[state.fileId];
      if (fileSession.token != state.token) {
        sendEmptyResponse(socket, 403, "Invalid Token");
        return;
      }

      QDir().mkpath(m_downloadDir);
      fileSession.targetPath = ensureUniqueFilePath(m_downloadDir, fileSession.fileName);
      state.outputFile = new QFile(fileSession.targetPath);
      if (!state.outputFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        delete state.outputFile;
        state.outputFile = nullptr;
        sendEmptyResponse(socket, 500, "Failed to open target file");
        return;
      }
      state.isStreamingUpload = true;
    }
  }

  if (state.isStreamingUpload && state.outputFile) {
    if (!state.buffer.isEmpty()) {
      const qint64 remaining = state.contentLength - state.bodyBytesReceived;
      const qint64 bytesToWrite = qMin<qint64>(state.buffer.size(), remaining);
      if (bytesToWrite > 0) {
        state.outputFile->write(state.buffer.constData(), bytesToWrite);
        state.bodyBytesReceived += bytesToWrite;
        state.buffer.remove(0, static_cast<qsizetype>(bytesToWrite));

        if (m_sessions.contains(state.sessionId)) {
          UploadSession &sess = m_sessions[state.sessionId];
          sess.receivedBytes += bytesToWrite;
          const IncomingFileSession &f = sess.files[state.fileId];
          Q_EMIT transferProgress(f.fileName, state.bodyBytesReceived, f.size);
        }
      }
    }

    if (state.bodyBytesReceived >= state.contentLength) {
      state.outputFile->flush();
      state.outputFile->close();
      delete state.outputFile;
      state.outputFile = nullptr;

      if (m_sessions.contains(state.sessionId)) {
        UploadSession &sess = m_sessions[state.sessionId];
        IncomingFileSession &f = sess.files[state.fileId];
        f.completed = true;
        sess.savedPaths.append(f.targetPath);

        Q_EMIT fileReceived(f.targetPath, sess.dropPos);

        bool allDone = true;
        for (const auto &item : std::as_const(sess.files)) {
          if (!item.completed) {
            allDone = false;
            break;
          }
        }
        if (allDone) {
          const QStringList paths = sess.savedPaths;
          const QPoint dropPos = sess.dropPos;
          m_sessions.remove(state.sessionId);
          Q_EMIT transferCompleted(paths, dropPos);
        }
      }

      sendEmptyResponse(socket, 200, "OK");
    }
    return;
  }

  if (state.buffer.size() >= state.contentLength) {
    const QByteArray body = state.buffer.left(static_cast<qsizetype>(state.contentLength));
    handleCompleteHttpRequest(socket, state, body);
  }
}

void LocalSendService::handleCompleteHttpRequest(
    QTcpSocket *socket, const HttpClientState &state, const QByteArray &body
)
{
  if (state.method == QStringLiteral("GET") && state.path == QStringLiteral("/api/localsend/v2/info")) {
    const QByteArray json = QJsonDocument(buildRegisterDto()).toJson(QJsonDocument::Compact);
    sendJsonResponse(socket, 200, "OK", json);
    return;
  }

  if (state.method == QStringLiteral("POST") && state.path == QStringLiteral("/api/localsend/v2/register")) {
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (doc.isObject()) {
      const QJsonObject obj = doc.object();
      LocalSendDevice dev;
      dev.alias = obj.value(QStringLiteral("alias")).toString();
      dev.version = obj.value(QStringLiteral("version")).toString();
      dev.deviceModel = obj.value(QStringLiteral("deviceModel")).toString();
      dev.deviceType = obj.value(QStringLiteral("deviceType")).toString();
      dev.fingerprint = obj.value(QStringLiteral("fingerprint")).toString();
      dev.ipAddress = socket->peerAddress().toString();
      if (dev.ipAddress.startsWith(QStringLiteral("::ffff:"))) {
        dev.ipAddress = dev.ipAddress.mid(7);
      }
      dev.port = static_cast<quint16>(obj.value(QStringLiteral("port")).toInt(kLocalSendDefaultPort));
      dev.protocol = obj.value(QStringLiteral("protocol")).toString(QStringLiteral("http"));
      if (!dev.fingerprint.isEmpty() && dev.fingerprint != m_fingerprint) {
        m_devices.insert(dev.ipAddress, dev);
        Q_EMIT deviceDiscovered(dev);
      }
    }
    const QByteArray json = QJsonDocument(buildRegisterDto()).toJson(QJsonDocument::Compact);
    sendJsonResponse(socket, 200, "OK", json);
    return;
  }

  if (state.method == QStringLiteral("POST") && state.path == QStringLiteral("/api/localsend/v2/prepare-upload")) {
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
      sendEmptyResponse(socket, 400, "Bad Request");
      return;
    }

    const QJsonObject root = doc.object();
    const QJsonObject filesObj = root.value(QStringLiteral("files")).toObject();
    if (filesObj.isEmpty()) {
      sendEmptyResponse(socket, 400, "No files provided");
      return;
    }

    UploadSession session;
    session.sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    session.senderAlias = root.value(QStringLiteral("info")).toObject().value(QStringLiteral("alias")).toString();
    if (root.contains(QStringLiteral("dropX")) && root.contains(QStringLiteral("dropY"))) {
      session.dropPos = QPoint(root.value(QStringLiteral("dropX")).toInt(-1), root.value(QStringLiteral("dropY")).toInt(-1));
    }

    QJsonObject responseFiles;
    QStringList fileNames;
    for (auto it = filesObj.begin(); it != filesObj.end(); ++it) {
      const QString fileId = it.key();
      const QJsonObject fObj = it.value().toObject();
      IncomingFileSession fSession;
      fSession.id = fileId;
      fSession.fileName = QFileInfo(fObj.value(QStringLiteral("fileName")).toString(QStringLiteral("received_file"))).fileName();
      fSession.size = static_cast<qint64>(fObj.value(QStringLiteral("size")).toDouble(0));
      fSession.fileType = fObj.value(QStringLiteral("fileType")).toString(QStringLiteral("application/octet-stream"));
      fSession.token = QUuid::createUuid().toString(QUuid::WithoutBraces);

      session.totalBytes += fSession.size;
      fileNames.append(fSession.fileName);
      responseFiles[fileId] = fSession.token;
      session.files.insert(fileId, fSession);
    }

    m_sessions.insert(session.sessionId, session);
    Q_EMIT transferStarted(session.sessionId, fileNames, session.totalBytes);

    QJsonObject resObj;
    resObj[QStringLiteral("sessionId")] = session.sessionId;
    resObj[QStringLiteral("files")] = responseFiles;

    sendJsonResponse(socket, 200, "OK", QJsonDocument(resObj).toJson(QJsonDocument::Compact));
    return;
  }

  if (state.method == QStringLiteral("POST") && state.path == QStringLiteral("/api/localsend/v2/cancel")) {
    const QString sessionId = state.query.value(QStringLiteral("sessionId"));
    m_sessions.remove(sessionId);
    sendEmptyResponse(socket, 200, "OK");
    return;
  }

  sendEmptyResponse(socket, 404, "Not Found");
}

void LocalSendService::sendJsonResponse(
    QTcpSocket *socket, int statusCode, const QByteArray &statusText, const QByteArray &jsonBody
)
{
  QByteArray response = "HTTP/1.1 " + QByteArray::number(statusCode) + " " + statusText + "\r\n";
  response += "Content-Type: application/json; charset=utf-8\r\n";
  response += "Content-Length: " + QByteArray::number(jsonBody.size()) + "\r\n";
  response += "Connection: close\r\n\r\n";
  response += jsonBody;
  socket->write(response);
  socket->flush();
  socket->disconnectFromHost();
}

void LocalSendService::sendEmptyResponse(QTcpSocket *socket, int statusCode, const QByteArray &statusText)
{
  QByteArray response = "HTTP/1.1 " + QByteArray::number(statusCode) + " " + statusText + "\r\n";
  response += "Content-Length: 0\r\n";
  response += "Connection: close\r\n\r\n";
  socket->write(response);
  socket->flush();
  socket->disconnectFromHost();
}

QString LocalSendService::ensureUniqueFilePath(const QString &dirPath, const QString &fileName) const
{
  const QFileInfo fi(QDir(dirPath).filePath(fileName));
  if (!fi.exists()) {
    return fi.absoluteFilePath();
  }

  const QString base = fi.completeBaseName();
  const QString ext = fi.suffix();
  int counter = 1;
  while (true) {
    const QString candidateName =
        ext.isEmpty() ? QStringLiteral("%1 (%2)").arg(base).arg(counter)
                      : QStringLiteral("%1 (%2).%3").arg(base).arg(counter).arg(ext);
    const QString candidatePath = QDir(dirPath).filePath(candidateName);
    if (!QFileInfo::exists(candidatePath)) {
      return candidatePath;
    }
    ++counter;
  }
}

void LocalSendService::sendFiles(
    const QString &targetHost, quint16 port, const QStringList &filePaths, const QPoint &dropPos
)
{
  if (filePaths.isEmpty() || targetHost.isEmpty()) {
    return;
  }

  QMimeDatabase mimeDb;
  QJsonObject filesJson;
  QHash<QString, QString> filePathsById;
  QStringList validFiles;

  for (int i = 0; i < filePaths.size(); ++i) {
    const QFileInfo fi(filePaths.at(i));
    if (!fi.exists() || !fi.isFile()) {
      continue;
    }

    const QString fileId = QStringLiteral("file_%1").arg(i);
    QJsonObject fileDto;
    fileDto[QStringLiteral("id")] = fileId;
    fileDto[QStringLiteral("fileName")] = fi.fileName();
    fileDto[QStringLiteral("size")] = fi.size();
    fileDto[QStringLiteral("fileType")] = mimeDb.mimeTypeForFile(fi).name();

    filesJson[fileId] = fileDto;
    filePathsById.insert(fileId, fi.absoluteFilePath());
    validFiles.append(fi.absoluteFilePath());
  }

  if (filesJson.isEmpty()) {
    return;
  }

  QJsonObject preparePayload;
  preparePayload[QStringLiteral("info")] = buildRegisterDto();
  preparePayload[QStringLiteral("files")] = filesJson;
  if (dropPos.x() >= 0 && dropPos.y() >= 0) {
    preparePayload[QStringLiteral("dropX")] = dropPos.x();
    preparePayload[QStringLiteral("dropY")] = dropPos.y();
  }

  const QUrl prepareUrl(QStringLiteral("http://%1:%2/api/localsend/v2/prepare-upload").arg(targetHost).arg(port));
  QNetworkRequest req(prepareUrl);
  req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
  req.setTransferTimeout(4000);

  QNetworkReply *reply = m_networkManager->post(req, QJsonDocument(preparePayload).toJson(QJsonDocument::Compact));
  connect(reply, &QNetworkReply::finished, this, [this, reply, targetHost, port, filePathsById, validFiles, dropPos]() {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
      Q_EMIT transferFailed(reply->errorString(), validFiles, dropPos);
      return;
    }

    const QJsonDocument resDoc = QJsonDocument::fromJson(reply->readAll());
    if (!resDoc.isObject()) {
      Q_EMIT transferFailed(tr("Invalid prepare-upload response"), validFiles, dropPos);
      return;
    }

    const QJsonObject resObj = resDoc.object();
    const QString sessionId = resObj.value(QStringLiteral("sessionId")).toString();
    const QJsonObject tokensObj = resObj.value(QStringLiteral("files")).toObject();

    QHash<QString, QString> tokens;
    QStringList acceptedIds;
    for (auto it = tokensObj.begin(); it != tokensObj.end(); ++it) {
      acceptedIds.append(it.key());
      tokens.insert(it.key(), it.value().toString());
    }

    if (sessionId.isEmpty() || acceptedIds.isEmpty()) {
      Q_EMIT transferFailed(tr("Receiver rejected file transfer"), validFiles, dropPos);
      return;
    }

    uploadNextFile(targetHost, port, sessionId, acceptedIds, tokens, filePathsById, 0, dropPos);
  });
}

void LocalSendService::uploadNextFile(
    const QString &targetHost, quint16 port, const QString &sessionId, const QStringList &fileIds,
    const QHash<QString, QString> &tokens, const QHash<QString, QString> &filePathsById, int index,
    const QPoint &dropPos
)
{
  if (index >= fileIds.size()) {
    Q_EMIT transferCompleted(filePathsById.values(), dropPos);
    return;
  }

  const QString fileId = fileIds.at(index);
  const QString token = tokens.value(fileId);
  const QString filePath = filePathsById.value(fileId);

  auto *file = new QFile(filePath);
  if (!file->open(QIODevice::ReadOnly)) {
    delete file;
    Q_EMIT transferFailed(tr("Could not open file for upload: %1").arg(filePath), filePathsById.values(), dropPos);
    return;
  }

  const QString fileName = QFileInfo(filePath).fileName();
  const qint64 totalSize = file->size();

  QUrl uploadUrl(QStringLiteral("http://%1:%2/api/localsend/v2/upload").arg(targetHost).arg(port));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("sessionId"), sessionId);
  query.addQueryItem(QStringLiteral("fileId"), fileId);
  query.addQueryItem(QStringLiteral("token"), token);
  uploadUrl.setQuery(query);

  QNetworkRequest req(uploadUrl);
  req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
  req.setHeader(QNetworkRequest::ContentLengthHeader, totalSize);

  QNetworkReply *reply = m_networkManager->post(req, file);
  file->setParent(reply);

  connect(reply, &QNetworkReply::uploadProgress, this, [this, fileName, totalSize](qint64 sent, qint64 total) {
    Q_EMIT transferProgress(fileName, sent, total > 0 ? total : totalSize);
  });

  connect(
      reply, &QNetworkReply::finished, this,
      [this, reply, targetHost, port, sessionId, fileIds, tokens, filePathsById, index, dropPos]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
          Q_EMIT transferFailed(reply->errorString(), filePathsById.values(), dropPos);
          return;
        }
        uploadNextFile(targetHost, port, sessionId, fileIds, tokens, filePathsById, index + 1, dropPos);
      }
  );
}

} // namespace deskflow::gui::transfer
