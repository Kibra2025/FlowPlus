/*
 * FlowPlus -- mouse, keyboard, and file sharing utility
 * SPDX-FileCopyrightText: (C) 2026 FlowPlus Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "LocalSendServiceTests.h"

#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTimer>
#include <QUrlQuery>

using namespace deskflow::gui::transfer;

namespace {
const QString kTransferId = QStringLiteral("11111111-1111-4111-8111-111111111111");

QJsonObject fileMetadata(const QString &name, qint64 size)
{
  return {{QStringLiteral("fileName"), name}, {QStringLiteral("size"), size}};
}

QByteArray prepareBody(const QString &id, const QJsonArray &manifest)
{
  QJsonObject files;
  for (qsizetype i = 0; i < manifest.size(); ++i) {
    files.insert(QStringLiteral("file_%1").arg(i), manifest.at(i));
  }
  return QJsonDocument(
             QJsonObject{
                 {QStringLiteral("files"), files},
                 {QStringLiteral("flowPlusTransferId"), id},
                 {QStringLiteral("dropX"), 999},
                 {QStringLiteral("dropY"), 888}
             }
  )
      .toJson(QJsonDocument::Compact);
}

QByteArray responseBody(const QByteArray &response)
{
  return response.mid(response.indexOf("\r\n\r\n") + 4);
}
} // namespace

void LocalSendServiceTests::init()
{
  m_directory = std::make_unique<QTemporaryDir>();
  QVERIFY(m_directory->isValid());
  m_service = std::make_unique<LocalSendService>();
  m_service->setDownloadDirectory(m_directory->path());
  m_service->setAlias(QStringLiteral("FlowPlus receiver test"));
  QVERIFY(m_service->start(0));
}

void LocalSendServiceTests::cleanup()
{
  m_service.reset();
  m_directory.reset();
}

QByteArray LocalSendServiceTests::rawRequest(const QByteArray &requestBytes)
{
  QTcpSocket socket;
  QEventLoop loop;
  QTimer timeout;
  timeout.setSingleShot(true);
  QByteArray response;
  connect(&socket, &QTcpSocket::connected, &loop, [&]() { socket.write(requestBytes); });
  connect(&socket, &QTcpSocket::readyRead, &loop, [&]() { response.append(socket.readAll()); });
  connect(&socket, &QTcpSocket::disconnected, &loop, &QEventLoop::quit);
  connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  socket.connectToHost(QHostAddress::LocalHost, m_service->serverPort());
  timeout.start(3000);
  loop.exec();
  response.append(socket.readAll());
  return response;
}

QByteArray
LocalSendServiceTests::request(const QByteArray &target, const QByteArray &body, const QByteArray &extraHeaders)
{
  return rawRequest(
      "POST " + target + " HTTP/1.1\r\nHost: localhost\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n" +
      extraHeaders + "Connection: close\r\n\r\n" + body
  );
}

QJsonObject LocalSendServiceTests::prepare(const QString &transferId, const QJsonArray &manifest)
{
  const auto response = request("/api/localsend/v2/prepare-upload", prepareBody(transferId, manifest));
  if (!response.startsWith("HTTP/1.1 200 ")) {
    qWarning().noquote() << "prepare-upload response:" << response;
    return {};
  }
  return QJsonDocument::fromJson(responseBody(response)).object();
}

QByteArray LocalSendServiceTests::uploadTarget(const QJsonObject &prepared, const QString &fileId) const
{
  QUrl url;
  url.setPath(QStringLiteral("/api/localsend/v2/upload"));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("sessionId"), prepared.value(QStringLiteral("sessionId")).toString());
  query.addQueryItem(QStringLiteral("fileId"), fileId);
  query.addQueryItem(
      QStringLiteral("token"), prepared.value(QStringLiteral("files")).toObject().value(fileId).toString()
  );
  url.setQuery(query);
  return url.toEncoded();
}

QStringList LocalSendServiceTests::downloads() const
{
  return QDir(m_directory->path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
}

void LocalSendServiceTests::unsolicitedUploadRejected()
{
  const QJsonArray manifest{fileMetadata(QStringLiteral("test.bin"), 10)};
  const auto response = request("/api/localsend/v2/prepare-upload", prepareBody(kTransferId, manifest));
  QVERIFY(response.startsWith("HTTP/1.1 403 "));
  QVERIFY(downloads().isEmpty());
}

void LocalSendServiceTests::authorizationMatchesManifestAndIsOneShot()
{
  const QJsonArray manifest{fileMetadata(QStringLiteral("test.bin"), 10)};
  QVERIFY(m_service->authorizeIncomingTransfer(QStringLiteral("127.0.0.1"), kTransferId, manifest, QPoint(4, 5)));
  const QJsonArray different{fileMetadata(QStringLiteral("different.bin"), 10)};
  QVERIFY(request("/api/localsend/v2/prepare-upload", prepareBody(kTransferId, different)).startsWith("HTTP/1.1 403 "));
  const auto prepared = prepare(kTransferId, manifest);
  QVERIFY(!prepared.isEmpty());
  QVERIFY(request("/api/localsend/v2/prepare-upload", prepareBody(kTransferId, manifest)).startsWith("HTTP/1.1 403 "));
  m_service->clearIncomingAuthorization(kTransferId);
  QVERIFY(request(uploadTarget(prepared, QStringLiteral("file_0")), QByteArray(10, 'x')).startsWith("HTTP/1.1 403 "));
  QVERIFY(downloads().isEmpty());
}

void LocalSendServiceTests::binaryBatchPreservesExistingFileAndSeparatesSenderCompletion()
{
  QTemporaryDir sourceDirectory;
  QVERIFY(sourceDirectory.isValid());
  QByteArray binary(300000, '\0');
  for (qsizetype i = 0; i < binary.size(); ++i) {
    binary[i] = static_cast<char>(i % 256);
  }
  const auto sourcePath = QDir(sourceDirectory.path()).filePath(QStringLiteral("payload.bin"));
  QFile source(sourcePath);
  QVERIFY(source.open(QIODevice::WriteOnly));
  QCOMPARE(source.write(binary), binary.size());
  source.close();
  const auto emptyPath = QDir(sourceDirectory.path()).filePath(QStringLiteral("empty.bin"));
  QFile empty(emptyPath);
  QVERIFY(empty.open(QIODevice::WriteOnly));
  empty.close();
  QFile existing(QDir(m_directory->path()).filePath(QStringLiteral("payload.bin")));
  QVERIFY(existing.open(QIODevice::WriteOnly));
  existing.write("existing content");
  existing.close();
  const QJsonArray manifest{
      fileMetadata(QStringLiteral("payload.bin"), binary.size()), fileMetadata(QStringLiteral("empty.bin"), 0)
  };
  const QPoint grantedPosition(-20, 50);
  QVERIFY(
      m_service->authorizeIncomingTransfer(QStringLiteral("::ffff:127.0.0.1"), kTransferId, manifest, grantedPosition)
  );
  LocalSendService sender;
  QSignalSpy sent(&sender, &LocalSendService::filesSent);
  QSignalSpy senderReceived(&sender, &LocalSendService::transferCompleted);
  QSignalSpy completed(m_service.get(), &LocalSendService::transferCompleted);
  QSignalSpy failed(&sender, &LocalSendService::transferFailed);
  connect(&sender, &LocalSendService::transferFailed, this, [](const QString &reason) {
    qWarning().noquote() << "Sender transfer failure:" << reason;
  });
  sender.sendFiles(
      QStringLiteral("127.0.0.1"), m_service->serverPort(), {sourcePath, emptyPath}, QPoint(999, 888), kTransferId
  );
  QTRY_COMPARE_WITH_TIMEOUT(sent.count(), 1, 5000);
  QCOMPARE(failed.count(), 0);
  QCOMPARE(completed.count(), 1);
  QCOMPARE(senderReceived.count(), 0);
  QCOMPARE(completed.first().at(1).toPoint(), grantedPosition);
  QCOMPARE(completed.first().at(2).toString(), kTransferId);
  QFile received(QDir(m_directory->path()).filePath(QStringLiteral("payload (1).bin")));
  QVERIFY(received.open(QIODevice::ReadOnly));
  QCOMPARE(received.readAll(), binary);
  QVERIFY(existing.open(QIODevice::ReadOnly));
  QCOMPARE(existing.readAll(), QByteArray("existing content"));
  QCOMPARE(downloads().size(), 3);
  QCOMPARE(QFileInfo(QDir(m_directory->path()).filePath(QStringLiteral("empty.bin"))).size(), 0);
}

void LocalSendServiceTests::authorizationRequiresTheApprovedSourceAddress()
{
  const QJsonArray manifest{fileMetadata(QStringLiteral("test.bin"), 10)};
  QVERIFY(m_service->authorizeIncomingTransfer(QStringLiteral("127.0.0.2"), kTransferId, manifest, QPoint()));
  QVERIFY(request("/api/localsend/v2/prepare-upload", prepareBody(kTransferId, manifest)).startsWith("HTTP/1.1 403 "));
  QVERIFY(downloads().isEmpty());
}

void LocalSendServiceTests::invalidDestinationRejectedWithoutConsumingGrant()
{
  const auto blockedPath = QDir(m_directory->path()).filePath(QStringLiteral("occupied"));
  QFile occupied(blockedPath);
  QVERIFY(occupied.open(QIODevice::WriteOnly));
  QCOMPARE(occupied.write("existing content"), qint64(16));
  occupied.close();
  const QJsonArray manifest{fileMetadata(QStringLiteral("received.bin"), 3)};
  QVERIFY(m_service->authorizeIncomingTransfer(QStringLiteral("127.0.0.1"), kTransferId, manifest, QPoint()));
  m_service->setDownloadDirectory(blockedPath);
  const auto response = request("/api/localsend/v2/prepare-upload", prepareBody(kTransferId, manifest));
  QVERIFY2(response.startsWith("HTTP/1.1 507 "), response.constData());
  m_service->setDownloadDirectory(m_directory->path());
  const auto prepared = prepare(kTransferId, manifest);
  QVERIFY(!prepared.isEmpty());
  QCOMPARE(downloads(), QStringList{QStringLiteral("occupied")});
  QVERIFY(request(uploadTarget(prepared, QStringLiteral("file_0")), "abc").startsWith("HTTP/1.1 200 "));
  QVERIFY(occupied.open(QIODevice::ReadOnly));
  QCOMPARE(occupied.readAll(), QByteArray("existing content"));
}

void LocalSendServiceTests::interruptedUploadRemovesTemporaryFiles()
{
  const QJsonArray manifest{fileMetadata(QStringLiteral("partial.bin"), 10)};
  QVERIFY(m_service->authorizeIncomingTransfer(QStringLiteral("127.0.0.1"), kTransferId, manifest, QPoint()));
  const auto prepared = prepare(kTransferId, manifest);
  QVERIFY(!prepared.isEmpty());
  QSignalSpy failures(m_service.get(), &LocalSendService::incomingTransferFailed);
  QSignalSpy completed(m_service.get(), &LocalSendService::transferCompleted);
  QSignalSpy progress(m_service.get(), &LocalSendService::transferProgress);
  QTcpSocket socket;
  socket.connectToHost(QHostAddress::LocalHost, m_service->serverPort());
  QTRY_COMPARE(socket.state(), QAbstractSocket::ConnectedState);
  socket.write(
      "POST " + uploadTarget(prepared, QStringLiteral("file_0")) +
      " HTTP/1.1\r\nHost: localhost\r\nContent-Length: 10\r\n\r\nabc"
  );
  // Linux may keep QTemporaryFile unnamed until the upload completes.
  QTRY_VERIFY(!progress.isEmpty() && progress.last().at(1).toLongLong() == 3);
  QCOMPARE(progress.last().at(0).toString(), QStringLiteral("partial.bin"));
  QCOMPARE(progress.last().at(2).toLongLong(), qint64(10));
  socket.abort();
  QTRY_COMPARE(failures.count(), 1);
  QVERIFY(downloads().isEmpty());
  QCOMPARE(completed.count(), 0);
  QCOMPARE(failures.first().at(1).toString(), kTransferId);
  QVERIFY(request(uploadTarget(prepared, QStringLiteral("file_0")), QByteArray(10, 'x')).startsWith("HTTP/1.1 403 "));
}

void LocalSendServiceTests::cancellationRemovesEntireStagedBatch()
{
  const QJsonArray manifest{
      fileMetadata(QStringLiteral("first.bin"), 3), fileMetadata(QStringLiteral("second.bin"), 10)
  };
  QVERIFY(m_service->authorizeIncomingTransfer(QStringLiteral("127.0.0.1"), kTransferId, manifest, QPoint()));
  const auto prepared = prepare(kTransferId, manifest);
  QVERIFY(!prepared.isEmpty());
  QSignalSpy files(m_service.get(), &LocalSendService::fileReceived);
  QSignalSpy completed(m_service.get(), &LocalSendService::transferCompleted);
  QVERIFY(request(uploadTarget(prepared, QStringLiteral("file_0")), "abc").startsWith("HTTP/1.1 200 "));
  QCOMPARE(files.count(), 0);
  QCOMPARE(completed.count(), 0);
  QCOMPARE(downloads().size(), 1);
  QSignalSpy progress(m_service.get(), &LocalSendService::transferProgress);
  QTcpSocket socket;
  socket.connectToHost(QHostAddress::LocalHost, m_service->serverPort());
  QTRY_COMPARE(socket.state(), QAbstractSocket::ConnectedState);
  socket.write(
      "POST " + uploadTarget(prepared, QStringLiteral("file_1")) +
      " HTTP/1.1\r\nHost: localhost\r\nContent-Length: 10\r\n\r\nabc"
  );
  QTRY_VERIFY(!progress.isEmpty() && progress.last().at(1).toLongLong() == 3);
  QCOMPARE(progress.last().at(0).toString(), QStringLiteral("second.bin"));
  QCOMPARE(progress.last().at(2).toLongLong(), qint64(10));
  const auto cancelTarget =
      "/api/localsend/v2/cancel?sessionId=" + prepared.value(QStringLiteral("sessionId")).toString().toUtf8();
  QVERIFY(request(cancelTarget).startsWith("HTTP/1.1 200 "));
  QTRY_VERIFY(downloads().isEmpty());
  QTRY_COMPARE(socket.state(), QAbstractSocket::UnconnectedState);
  QCOMPARE(files.count(), 0);
  QCOMPARE(completed.count(), 0);
}

void LocalSendServiceTests::uploadRequiresExactSizeAndToken()
{
  const QJsonArray manifest{fileMetadata(QStringLiteral("size.bin"), 10)};
  QVERIFY(m_service->authorizeIncomingTransfer(QStringLiteral("127.0.0.1"), kTransferId, manifest, QPoint()));
  const auto prepared = prepare(kTransferId, manifest);
  QVERIFY(!prepared.isEmpty());
  QVERIFY(request(uploadTarget(prepared, QStringLiteral("file_0")), "abc").startsWith("HTTP/1.1 400 "));
  auto wrongToken = prepared;
  wrongToken[QStringLiteral("files")] = QJsonObject{{QStringLiteral("file_0"), QStringLiteral("wrong")}};
  QVERIFY(request(uploadTarget(wrongToken, QStringLiteral("file_0")), QByteArray(10, 'x')).startsWith("HTTP/1.1 403 "));
  QVERIFY(downloads().isEmpty());
  QVERIFY(request(uploadTarget(prepared, QStringLiteral("file_0")), QByteArray(10, 'x')).startsWith("HTTP/1.1 200 "));
}

void LocalSendServiceTests::malformedRequests_data()
{
  QTest::addColumn<QByteArray>("requestBytes");
  QTest::addColumn<QByteArray>("status");
  QTest::newRow("negative length") << QByteArray(
                                          "POST /api/localsend/v2/register HTTP/1.1\r\nContent-Length: -1\r\n\r\n"
                                      )
                                   << QByteArray("400");
  QTest::newRow(
      "duplicate length"
  ) << QByteArray("POST /api/localsend/v2/register HTTP/1.1\r\nContent-Length: 0\r\nContent-Length: 0\r\n\r\n")
    << QByteArray("400");
  QTest::newRow("invalid length") << QByteArray(
                                         "POST /api/localsend/v2/register HTTP/1.1\r\nContent-Length: zero\r\n\r\n"
                                     )
                                  << QByteArray("400");
  QTest::newRow("missing length") << QByteArray("POST /api/localsend/v2/register HTTP/1.1\r\nHost: localhost\r\n\r\n")
                                  << QByteArray("411");
  QTest::newRow("chunked") << QByteArray(
                                  "POST /api/localsend/v2/register HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n"
                              )
                           << QByteArray("501");
  QTest::newRow("large JSON") << QByteArray(
                                     "POST /api/localsend/v2/register HTTP/1.1\r\nContent-Length: 1048577\r\n\r\n"
                                 )
                              << QByteArray("413");
  QTest::newRow("large headers") << QByteArray("GET /api/localsend/v2/info HTTP/1.1\r\nX-Padding: ") +
                                        QByteArray(17000, 'x') + "\r\n\r\n"
                                 << QByteArray("431");
  QTest::newRow(
      "duplicate query"
  ) << QByteArray("POST /api/localsend/v2/cancel?sessionId=a&sessionId=b HTTP/1.1\r\nContent-Length: 0\r\n\r\n")
    << QByteArray("400");
}

void LocalSendServiceTests::malformedRequests()
{
  QFETCH(QByteArray, requestBytes);
  QFETCH(QByteArray, status);
  const auto response = rawRequest(requestBytes);
  QVERIFY2(response.startsWith("HTTP/1.1 " + status + " "), response.constData());
  QVERIFY(downloads().isEmpty());
}

void LocalSendServiceTests::senderDoesNotFallbackAfterPolicyRejection()
{
  QTemporaryDir sourceDirectory;
  QFile source(QDir(sourceDirectory.path()).filePath(QStringLiteral("test.bin")));
  QVERIFY(source.open(QIODevice::WriteOnly));
  source.write("abc");
  source.close();
  LocalSendService sender;
  QSignalSpy failed(&sender, &LocalSendService::transferFailed);
  sender.sendFiles(QStringLiteral("127.0.0.1"), m_service->serverPort(), {source.fileName()}, QPoint(), kTransferId);
  QTRY_COMPARE(failed.count(), 1);
  QCOMPARE(failed.first().at(3).toBool(), false);
  QCOMPARE(failed.first().at(4).toString(), kTransferId);
  QVERIFY(downloads().isEmpty());
}

void LocalSendServiceTests::senderCanFallbackBeforeUploadOnConnectionFailure()
{
  QTcpServer closedServer;
  QVERIFY(closedServer.listen(QHostAddress::LocalHost, 0));
  const auto port = closedServer.serverPort();
  closedServer.close();
  QFile source(QDir(m_directory->path()).filePath(QStringLiteral("test.bin")));
  QVERIFY(source.open(QIODevice::WriteOnly));
  source.write("abc");
  source.close();
  LocalSendService sender;
  QSignalSpy failed(&sender, &LocalSendService::transferFailed);
  sender.sendFiles(QStringLiteral("127.0.0.1"), port, {source.fileName()}, QPoint(), kTransferId);
  QTRY_COMPARE(failed.count(), 1);
  QCOMPARE(failed.first().at(3).toBool(), true);
  QCOMPARE(failed.first().at(1).toStringList(), QStringList{source.fileName()});
}

void LocalSendServiceTests::cancelledSenderStopsOutstandingRequest()
{
  QTcpServer pendingReceiver;
  QVERIFY(pendingReceiver.listen(QHostAddress::LocalHost, 0));
  QFile source(QDir(m_directory->path()).filePath(QStringLiteral("test.bin")));
  QVERIFY(source.open(QIODevice::WriteOnly));
  source.write("abc");
  source.close();
  LocalSendService sender;
  QSignalSpy failed(&sender, &LocalSendService::transferFailed);
  QSignalSpy sent(&sender, &LocalSendService::filesSent);
  sender.sendFiles(
      QStringLiteral("127.0.0.1"), pendingReceiver.serverPort(), {source.fileName()}, QPoint(), kTransferId
  );
  QTRY_VERIFY(pendingReceiver.hasPendingConnections());
  auto *connection = pendingReceiver.nextPendingConnection();
  QVERIFY(connection);
  sender.cancelOutgoingTransfer(kTransferId);
  QTRY_COMPARE(connection->state(), QAbstractSocket::UnconnectedState);
  QCOMPARE(failed.count(), 0);
  QCOMPARE(sent.count(), 0);
}

void LocalSendServiceTests::senderNeverFallbacksAfterUploadFailure()
{
  QTcpServer receiver;
  QVERIFY(receiver.listen(QHostAddress::LocalHost, 0));
  connect(&receiver, &QTcpServer::newConnection, this, [&receiver]() {
    auto *socket = receiver.nextPendingConnection();
    const auto buffer = std::make_shared<QByteArray>();
    connect(socket, &QTcpSocket::readyRead, socket, [socket, buffer]() {
      buffer->append(socket->readAll());
      const auto headerEnd = buffer->indexOf("\r\n\r\n");
      if (headerEnd < 0 || socket->property("responded").toBool()) {
        return;
      }
      socket->setProperty("responded", true);
      QByteArray body;
      QByteArray status = "200 OK";
      if (buffer->startsWith("POST /api/localsend/v2/prepare-upload ")) {
        body = R"({"sessionId":"test-session","files":{"file_0":"test-token"}})";
      } else if (buffer->startsWith("POST /api/localsend/v2/upload?")) {
        status = "500 Failed";
      }
      socket->write(
          "HTTP/1.1 " + status + "\r\nContent-Length: " + QByteArray::number(body.size()) +
          "\r\nConnection: close\r\n\r\n" + body
      );
      socket->disconnectFromHost();
    });
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
  });
  QFile source(QDir(m_directory->path()).filePath(QStringLiteral("test.bin")));
  QVERIFY(source.open(QIODevice::WriteOnly));
  source.write("abc");
  source.close();
  LocalSendService sender;
  QSignalSpy failed(&sender, &LocalSendService::transferFailed);
  QSignalSpy sent(&sender, &LocalSendService::filesSent);
  sender.sendFiles(QStringLiteral("127.0.0.1"), receiver.serverPort(), {source.fileName()}, QPoint(), kTransferId);
  QTRY_COMPARE(failed.count(), 1);
  QCOMPARE(failed.first().at(3).toBool(), false);
  QCOMPARE(sent.count(), 0);
}

void LocalSendServiceTests::invalidLocalFilesReportFailure()
{
  LocalSendService sender;
  QSignalSpy failed(&sender, &LocalSendService::transferFailed);
  sender.sendFiles(
      QStringLiteral("127.0.0.1"), m_service->serverPort(),
      {QDir(m_directory->path()).filePath(QStringLiteral("missing.bin"))}, QPoint(), kTransferId
  );
  QCOMPARE(failed.count(), 1);
  QCOMPARE(failed.first().at(3).toBool(), false);
}

QTEST_GUILESS_MAIN(LocalSendServiceTests)
