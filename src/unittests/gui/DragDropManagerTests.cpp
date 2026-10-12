/*
 * FlowPlus -- mouse, keyboard, and file sharing utility
 * SPDX-FileCopyrightText: (C) 2026 FlowPlus Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/transfer/DragOverlayWidget.h"
#include "gui/transfer/TransferUtils.h"

#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QLabel>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#if defined(Q_OS_WIN)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace deskflow::gui::transfer {
class DragDropManagerTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void rejectUnsafeManifest_data();
  void rejectUnsafeManifest();
  void boundTransferResources();
  void preserveExistingFiles();
  void collisionNamesStayPortable();
  void requireCanonicalTransferIds();
  void doNotDropWhenCursorPauses();
  void overlayTreatsNamesAsPlainText();
};

void DragDropManagerTests::rejectUnsafeManifest_data()
{
  QTest::addColumn<QString>("fileName");
  for (const auto &name :
       {QString(), QStringLiteral(".."), QStringLiteral("../outside.txt"), QStringLiteral("folder/file.txt"),
        QStringLiteral("folder\\file.txt"), QStringLiteral("C:secret.txt"), QStringLiteral("CON.txt"),
        QStringLiteral("LPT1.log"), QStringLiteral("bad\nname.txt"), QStringLiteral("trailing."),
        QStringLiteral("trailing "), QStringLiteral("CON .txt"), QStringLiteral("COM\u00b9.txt"),
        QStringLiteral("LPT\u00b2.txt"), QStringLiteral("bad\x7f.txt"), QString(128, QChar(0x00e8)),
        QString(256, 'a')}) {
    QTest::newRow(qPrintable(name.isEmpty() ? QStringLiteral("empty") : name)) << name;
  }
}

void DragDropManagerTests::rejectUnsafeManifest()
{
  QFETCH(QString, fileName);
  qint64 total = 0;
  QVERIFY(!validateTransferManifest(
      QJsonArray{QJsonObject{{QStringLiteral("fileName"), fileName}, {QStringLiteral("size"), 1}}}, total
  ));
}

void DragDropManagerTests::boundTransferResources()
{
  const auto entry = [](const QJsonValue &size) {
    return QJsonObject{{QStringLiteral("fileName"), QStringLiteral("report.txt")}, {QStringLiteral("size"), size}};
  };
  qint64 total = 0;
  QVERIFY(!validateTransferManifest({}, total));
  QVERIFY(!validateTransferManifest(QJsonArray{entry(-1)}, total));
  QVERIFY(!validateTransferManifest(QJsonArray{entry(1.5)}, total));
  QVERIFY(!validateTransferManifest(QJsonArray{entry(QStringLiteral("12"))}, total));
  QVERIFY(!validateTransferManifest(QJsonArray{entry(kMaxTransferBytes + 1)}, total));
  QVERIFY(!validateTransferManifest(QJsonArray{entry(kMaxTransferBytes), entry(1)}, total));
  QJsonArray files;
  for (int index = 0; index < kMaxTransferFiles; ++index) {
    files.append(entry(0));
  }
  QVERIFY(validateTransferManifest(files, total));
  QCOMPARE(total, 0);
  files.append(entry(0));
  QVERIFY(!validateTransferManifest(files, total));
  QVERIFY(validateTransferManifest(QJsonArray{entry(kMaxTransferBytes)}, total));
  QCOMPARE(total, kMaxTransferBytes);
}

void DragDropManagerTests::preserveExistingFiles()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  QFile existing(QDir(directory.path()).filePath(QStringLiteral("report.txt")));
  QVERIFY(existing.open(QIODevice::WriteOnly));
  QCOMPARE(existing.write("original"), qint64(8));
  existing.close();
  QCOMPARE(
      uniqueTransferDestination(directory.path(), QStringLiteral("report.txt")),
      QDir(directory.path()).filePath(QStringLiteral("report (1).txt"))
  );
  QVERIFY(uniqueTransferDestination(directory.path(), QStringLiteral("../report.txt")).isEmpty());
  QVERIFY(existing.open(QIODevice::ReadOnly));
  QCOMPARE(existing.readAll(), QByteArray("original"));
}

void DragDropManagerTests::collisionNamesStayPortable()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto name = QString(251, 'a') + QStringLiteral(".txt");
  QVERIFY(isPortableFileName(name));
  QFile existing(QDir(directory.path()).filePath(name));
  if (!existing.open(QIODevice::WriteOnly)) {
    QSKIP("This filesystem does not support maximum length file names");
  }
  existing.close();
  const auto destination = uniqueTransferDestination(directory.path(), name);
  QVERIFY(!destination.isEmpty());
  const auto uniqueName = QFileInfo(destination).fileName();
  QVERIFY(isPortableFileName(uniqueName));
  QVERIFY(uniqueName.endsWith(QStringLiteral(" (1).txt")));
  QVERIFY(uniqueName != name);
}

void DragDropManagerTests::requireCanonicalTransferIds()
{
  const auto id = QStringLiteral("12345678-1234-4abc-8def-123456789abc");
  QVERIFY(isValidTransferId(id));
  QVERIFY(!isValidTransferId(QString()));
  QVERIFY(!isValidTransferId(QUuid().toString(QUuid::WithoutBraces)));
  QVERIFY(!isValidTransferId(QStringLiteral("{") + id + QStringLiteral("}")));
  QVERIFY(!isValidTransferId(id.toUpper()));
}

void DragDropManagerTests::doNotDropWhenCursorPauses()
{
#if defined(Q_OS_WIN)
  if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) {
    QSKIP("A physical mouse button is held during this desktop test");
  }
#endif
  DragOverlayWidget overlay;
  QSignalSpy dropped(&overlay, &DragOverlayWidget::dropDetected);
  overlay.showDragging(QStringLiteral("report.txt"), QStringLiteral("Release to receive"));
  QTest::qWait(1500);
  QCOMPARE(dropped.count(), 0);
  QCOMPARE(overlay.state(), DragOverlayWidget::State::Dragging);
  overlay.hideOverlay();
}

void DragDropManagerTests::overlayTreatsNamesAsPlainText()
{
  DragOverlayWidget overlay;
  overlay.showDragging(QStringLiteral("<b>report.txt</b>"), QStringLiteral("Release"));
  const auto labels = overlay.findChildren<QLabel *>();
  QCOMPARE(labels.size(), 2);
  for (const auto *label : labels) {
    QCOMPARE(label->textFormat(), Qt::PlainText);
  }
  overlay.showTransferring(QStringLiteral("report.txt"), 150);
  QCOMPARE(overlay.state(), DragOverlayWidget::State::Transferring);
  overlay.hideOverlay();
}
} // namespace deskflow::gui::transfer

QTEST_MAIN(deskflow::gui::transfer::DragDropManagerTests)
#include "DragDropManagerTests.moc"
