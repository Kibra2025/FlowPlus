/*
 * SPDX-FileCopyrightText: (C) 2026 FlowPlus contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/DeskflowException.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"
#include "io/IStream.h"
#include <QTest>
#include <cstring>

#ifndef NOLOGGING
#include "base/Log.h"
#endif

class MemoryStream : public deskflow::IStream
{
public:
  QByteArray bytes;
  uint32_t read(void *buffer, uint32_t count) override
  {
    const auto size = std::min(count, static_cast<uint32_t>(bytes.size()));
    if (buffer && size) {
      std::memcpy(buffer, bytes.constData(), size);
    }
    bytes.remove(0, size);
    return size;
  }
  void close() override
  {
  }
  void write(const void *, uint32_t) override
  {
  }
  void flush() override
  {
  }
  void shutdownInput() override
  {
  }
  void shutdownOutput() override
  {
  }
  void *getEventTarget() const override
  {
    return const_cast<MemoryStream *>(this);
  }
  bool isReady() const override
  {
    return !bytes.isEmpty();
  }
  uint32_t getSize() const override
  {
    return static_cast<uint32_t>(bytes.size());
  }
};

class ProtocolUtilLimitsTests : public QObject
{
  Q_OBJECT
private:
#ifndef NOLOGGING
  Log m_log;
#endif

private Q_SLOTS:
  void rejectsOversizedEmbeddedLengthBeforeReadingBody()
  {
    MemoryStream stream;
    stream.bytes = QByteArray("\x00\x10\x00\x01", 4) + "untouched";
    std::string output;
    QVERIFY_THROWS_EXCEPTION(BadClientException, ProtocolUtil::readf(&stream, "%s", &output));
    QCOMPARE(stream.bytes, QByteArray("untouched"));
    QVERIFY(output.empty());
  }

  void acceptsBinaryVariableString()
  {
    MemoryStream stream;
    const QByteArray expected("a\0b\n", 4);
    stream.bytes = QByteArray("\0\0\0\4", 4) + expected;
    std::string output;
    QVERIFY(ProtocolUtil::readf(&stream, "%s", &output));
    QCOMPARE(QByteArray::fromStdString(output), expected);
  }

  void rejectsTruncatedEmbeddedLength()
  {
    MemoryStream stream;
    stream.bytes = QByteArray("\0\0", 2);
    std::string output;
    QVERIFY(!ProtocolUtil::readf(&stream, "%s", &output));
  }
};

QTEST_GUILESS_MAIN(ProtocolUtilLimitsTests)
#include "ProtocolUtilLimitsTests.moc"
