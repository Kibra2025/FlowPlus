/*
 * FlowPlus -- mouse, keyboard, and file sharing utility
 * SPDX-FileCopyrightText: (C) 2026 FlowPlus Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "TransferUtils.h"

#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPoint>
#include <QSet>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryFile>
#include <QTimer>
#include <QUdpSocket>
#include <QUrl>

#include <memory>

namespace deskflow::gui::transfer {

constexpr quint16 kLocalSendDefaultPort = 53317;
constexpr auto kLocalSendMulticastGroup = "224.0.0.167";
constexpr auto kLocalSendProtocolVersion = "2.1";

struct LocalSendDevice
{
  QString alias;
  QString version;
  QString deviceModel;
  QString deviceType;
  QString fingerprint;
  QString ipAddress;
  quint16 port = kLocalSendDefaultPort;
  QString protocol = QStringLiteral("http");
};

// LocalSend v2 HTTP transport. Discovery is never an authorization to receive files.
// Incoming offers must match a one-shot grant supplied by the authenticated drag channel.
class LocalSendService : public QObject
{
  Q_OBJECT

public:
  explicit LocalSendService(QObject *parent = nullptr);
  ~LocalSendService() override;

  bool start(quint16 port = kLocalSendDefaultPort);
  void stop();

  bool isRunning() const
  {
    return m_tcpServer->isListening();
  }
  quint16 serverPort() const
  {
    return m_tcpServer->serverPort();
  }

  QString downloadDirectory() const;
  void setDownloadDirectory(const QString &dirPath);
  void setAlias(const QString &alias);
  bool authorizeIncomingTransfer(
      const QString &peerAddress, const QString &transferId, const QJsonArray &manifest, const QPoint &dropPos
  );
  void clearIncomingAuthorization(const QString &transferId);
  void cancelOutgoingTransfer(const QString &transferId);
  void sendAnnouncement();
  void sendFiles(
      const QString &targetHost, quint16 port, const QStringList &filePaths, const QPoint &dropPos = QPoint(-1, -1),
      const QString &transferId = {}
  );

  QList<LocalSendDevice> discoveredDevices() const
  {
    return m_devices.values();
  }

Q_SIGNALS:
  void deviceDiscovered(const deskflow::gui::transfer::LocalSendDevice &device);
  void transferStarted(const QString &sessionId, const QStringList &fileNames, qint64 totalBytes);
  void transferProgress(const QString &fileName, qint64 bytesTransferred, qint64 totalBytes);
  void fileReceived(const QString &savedFilePath, const QPoint &dropPos);
  void transferCompleted(const QStringList &filePaths, const QPoint &dropPos, const QString &transferId);
  void filesSent(const QStringList &filePaths, const QPoint &dropPos, const QString &transferId);
  void transferFailed(
      const QString &reason, const QStringList &remainingFiles, const QPoint &dropPos, bool allowFallback,
      const QString &transferId
  );
  void incomingTransferFailed(const QString &reason, const QString &transferId);

private Q_SLOTS:
  void onNewTcpConnection();
  void onUdpReadyRead();
  void expireInactiveTransfers();

private:
  struct IncomingGrant
  {
    QString peerAddress;
    QStringList manifest;
    QPoint dropPos;
    qint64 expiresAt = 0;
  };

  struct IncomingFileSession
  {
    QString fileName;
    qint64 size = 0;
    QString token;
    QString stagingPath;
    bool uploading = false;
    bool completed = false;
  };

  struct UploadSession
  {
    QString transferId;
    QString peerAddress;
    QPoint dropPos;
    qint64 lastActivity = 0;
    QHash<QString, IncomingFileSession> files;
    QStringList fileIds;
  };

  struct HttpClientState
  {
    QByteArray buffer;
    bool headersParsed = false;
    bool responseSent = false;
    QString method;
    QString path;
    QHash<QString, QString> query;
    qint64 contentLength = 0;
    qint64 bodyBytesReceived = 0;
    qint64 connectedAt = 0;
    qint64 lastActivity = 0;
    bool isStreamingUpload = false;
    QString sessionId;
    QString fileId;
    QTemporaryFile *outputFile = nullptr;
  };

  struct OutgoingTransfer
  {
    QString transferId;
    QUrl baseUrl;
    QString sessionId;
    QStringList fileIds;
    QStringList paths;
    QHash<QString, QString> tokens;
    QHash<QString, QString> pathsById;
    QHash<QString, qint64> sizesById;
    QPoint dropPos;
    quint64 generation = 0;
  };

  void handleClientSocketData(QTcpSocket *socket);
  bool parseHeaders(QTcpSocket *socket);
  void handleCompleteHttpRequest(QTcpSocket *socket, const QByteArray &body);
  void receiveUpload(QTcpSocket *socket);
  void finishIncomingFile(QTcpSocket *socket);
  void cleanupClient(QTcpSocket *socket);
  void abortIncomingSession(const QString &sessionId, const QString &reason);
  void
  sendJsonResponse(QTcpSocket *socket, int statusCode, const QByteArray &statusText, const QByteArray &jsonBody = {});
  void sendEmptyResponse(QTcpSocket *socket, int statusCode, const QByteArray &statusText);
  void uploadNextFile(const std::shared_ptr<OutgoingTransfer> &transfer, int index);
  void failOutgoingTransfer(
      const std::shared_ptr<OutgoingTransfer> &transfer, const QString &reason, int nextIndex, bool allowFallback
  );
  void cancelRemoteSession(const std::shared_ptr<OutgoingTransfer> &transfer);
  void trackReply(QNetworkReply *reply, int hardTimeoutMs = 0);
  QJsonObject buildRegisterDto() const;
  bool rememberDevice(const QJsonObject &dto, const QHostAddress &address);

  QTcpServer *m_tcpServer;
  QUdpSocket *m_udpSocket;
  QNetworkAccessManager *m_networkManager;
  QTimer *m_announceTimer;
  QTimer *m_cleanupTimer;
  QString m_downloadDir;
  QString m_deviceAlias;
  QString m_fingerprint;
  QHash<QString, LocalSendDevice> m_devices;
  QHash<QString, qint64> m_deviceLastSeen;
  QHash<QString, qint64> m_lastRegistration;
  QHash<QString, IncomingGrant> m_grants;
  QHash<QString, UploadSession> m_sessions;
  QHash<QTcpSocket *, HttpClientState> m_httpClients;
  QSet<QNetworkReply *> m_activeReplies;
  QHash<QString, std::shared_ptr<OutgoingTransfer>> m_outgoingTransfers;
  quint64 m_generation = 0;
};

} // namespace deskflow::gui::transfer
