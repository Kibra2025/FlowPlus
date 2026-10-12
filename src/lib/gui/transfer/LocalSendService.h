/*
 * FlowPlus -- mouse, keyboard, and file sharing utility
 * SPDX-FileCopyrightText: (C) 2026 FlowPlus Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QFile>
#include <QHash>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>

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
    return m_tcpServer && m_tcpServer->isListening();
  }
  quint16 serverPort() const
  {
    return m_tcpServer ? m_tcpServer->serverPort() : 0;
  }

  QString downloadDirectory() const;
  void setDownloadDirectory(const QString &dirPath);

  void sendAnnouncement();
  void sendFiles(
      const QString &targetHost, quint16 port, const QStringList &filePaths, const QPoint &dropPos = QPoint(-1, -1)
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
  void transferCompleted(const QStringList &filePaths, const QPoint &dropPos);
  void transferFailed(const QString &reason, const QStringList &originalFiles, const QPoint &dropPos);

private Q_SLOTS:
  void onNewTcpConnection();
  void onUdpReadyRead();

private:
  struct IncomingFileSession
  {
    QString id;
    QString fileName;
    qint64 size = 0;
    QString fileType;
    QString token;
    QString targetPath;
    bool completed = false;
  };

  struct UploadSession
  {
    QString sessionId;
    QString senderAlias;
    QPoint dropPos = QPoint(-1, -1);
    qint64 totalBytes = 0;
    qint64 receivedBytes = 0;
    QHash<QString, IncomingFileSession> files;
    QStringList savedPaths;
  };

  struct HttpClientState
  {
    QByteArray buffer;
    bool headersParsed = false;
    QString method;
    QString path;
    QHash<QString, QString> query;
    qint64 contentLength = 0;
    qint64 bodyBytesReceived = 0;
    bool isStreamingUpload = false;
    QString sessionId;
    QString fileId;
    QString token;
    QFile *outputFile = nullptr;
  };

  void handleClientSocketData(QTcpSocket *socket);
  void handleCompleteHttpRequest(QTcpSocket *socket, const HttpClientState &state, const QByteArray &body);
  void sendJsonResponse(QTcpSocket *socket, int statusCode, const QByteArray &statusText, const QByteArray &jsonBody);
  void sendEmptyResponse(QTcpSocket *socket, int statusCode, const QByteArray &statusText);
  void uploadNextFile(
      const QString &targetHost, quint16 port, const QString &sessionId, const QStringList &fileIds,
      const QHash<QString, QString> &tokens, const QHash<QString, QString> &filePathsById, int index,
      const QPoint &dropPos
  );
  QJsonObject buildRegisterDto() const;
  QString ensureUniqueFilePath(const QString &dirPath, const QString &fileName) const;

  QTcpServer *m_tcpServer = nullptr;
  QUdpSocket *m_udpSocket = nullptr;
  QNetworkAccessManager *m_networkManager = nullptr;
  QTimer *m_announceTimer = nullptr;
  QString m_downloadDir;
  QString m_fingerprint;
  QHash<QString, LocalSendDevice> m_devices;
  QHash<QString, UploadSession> m_sessions;
  QHash<QTcpSocket *, HttpClientState> m_httpClients;
};

} // namespace deskflow::gui::transfer
