/*
 * SPDX-FileCopyrightText: (C) 2026 FlowPlus contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "common/TransferProtocol.h"
#include <QTest>

using namespace deskflow::transfer;

class TransferProtocolTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void preservesBinaryDataAndScreenIdentity()
  {
    const QByteArray data("\0\n\xff=\n", 5);
    RoutedPayload received;
    QVERIFY(decode(encode(QStringLiteral("Mac Studio"), QStringLiteral("Windows PC"), data), received));
    QCOMPARE(received.source, QStringLiteral("Mac Studio"));
    QCOMPARE(received.target, QStringLiteral("Windows PC"));
    QCOMPARE(received.data, data);
  }

  void refusesOversizedPayloadAndScreenNames()
  {
    QVERIFY(encode({}, {}, QByteArray(kMaxPayload + 1, 'x')).isEmpty());
    QVERIFY(encode(QString(256, 'x'), {}, {}).isEmpty());
    QVERIFY(encode(QString(QChar::Null), {}, {}).isEmpty());
    RoutedPayload received;
    QVERIFY(!decode(QByteArray(kMaxRouteHeader + 1, 'x') + '\n', received));
  }

  void rejectsMalformedNumberedIpc_data()
  {
    QTest::addColumn<QString>("arguments");
    QTest::newRow("missing separator") << QStringLiteral("invalid");
    QTest::newRow("empty number") << QStringLiteral(":YWJj");
    QTest::newRow("invalid number") << QStringLiteral("x:YWJj");
    QTest::newRow("negative") << QStringLiteral("-1:YWJj");
    QTest::newRow("oversized mark") << QStringLiteral("256:YWJj");
    QTest::newRow("invalid base64") << QStringLiteral("1:@@@@");
    QTest::newRow("invalid route") << QStringLiteral("1:YWJj");
  }

  void rejectsMalformedNumberedIpc()
  {
    QFETCH(QString, arguments);
    NumberedPayload result;
    QVERIFY(!decodeNumberedIpc(arguments, 255, result));
  }

  void numberedIpcPreservesBinaryPayload()
  {
    const QByteArray data("\0\xff\n", 3);
    const auto wire = encode(QStringLiteral("sender"), QStringLiteral("receiver"), data);
    NumberedPayload result;
    QVERIFY(decodeNumberedIpc(QStringLiteral("3:") + QString::fromLatin1(wire.toBase64()), 255, result));
    QCOMPARE(result.number, 3U);
    QCOMPARE(result.wire, wire);
    QCOMPARE(result.route.data, data);
  }

  void rejectsMalformedRoutingHeaders_data()
  {
    QTest::addColumn<QByteArray>("wire");
    QTest::newRow("legacy") << QByteArray("{\"files\":[]}\n");
    QTest::newRow("version") << QByteArray("{\"flowplusRoute\":2,\"source\":\"pc\",\"target\":\"mac\"}\n");
    QTest::newRow("types") << QByteArray("{\"flowplusRoute\":1,\"source\":3,\"target\":\"mac\"}\n");
    QTest::newRow("partial") << QByteArray("{\"flowplusRoute\":1");
    QTest::newRow("nul") << QByteArray("{\"flowplusRoute\":1,\"source\":\"pc\\u0000\",\"target\":\"mac\"}\n");
  }

  void rejectsMalformedRoutingHeaders()
  {
    QFETCH(QByteArray, wire);
    RoutedPayload received;
    QVERIFY(!decode(wire, received));
  }
};

QTEST_GUILESS_MAIN(TransferProtocolTests)
#include "TransferProtocolTests.moc"
